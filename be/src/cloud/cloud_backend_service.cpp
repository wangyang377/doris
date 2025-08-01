// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "cloud/cloud_backend_service.h"

#include <brpc/controller.h>

#include "cloud/cloud_storage_engine.h"
#include "cloud/cloud_tablet.h"
#include "cloud/cloud_tablet_hotspot.h"
#include "cloud/cloud_tablet_mgr.h"
#include "cloud/cloud_warm_up_manager.h"
#include "common/config.h"
#include "common/logging.h"
#include "common/status.h"
#include "io/cache/block_file_cache_downloader.h"
#include "io/cache/block_file_cache_factory.h"
#include "runtime/stream_load/stream_load_context.h"
#include "runtime/stream_load/stream_load_recorder.h"
#include "util/brpc_client_cache.h" // BrpcClientCache
#include "util/thrift_server.h"

namespace doris {

CloudBackendService::CloudBackendService(CloudStorageEngine& engine, ExecEnv* exec_env)
        : BaseBackendService(exec_env), _engine(engine) {}

CloudBackendService::~CloudBackendService() = default;

Status CloudBackendService::create_service(CloudStorageEngine& engine, ExecEnv* exec_env, int port,
                                           std::unique_ptr<ThriftServer>* server,
                                           std::shared_ptr<doris::CloudBackendService> service) {
    service->_agent_server->cloud_start_workers(engine, exec_env);
    // TODO: do we want a BoostThreadFactory?
    // TODO: we want separate thread factories here, so that fe requests can't starve
    // be requests
    // std::shared_ptr<TProcessor> be_processor = std::make_shared<BackendServiceProcessor>(service);
    auto be_processor = std::make_shared<BackendServiceProcessor>(service);

    *server = std::make_unique<ThriftServer>("backend", be_processor, port,
                                             config::be_service_threads);

    LOG(INFO) << "Doris CloudBackendService listening on " << port;

    return Status::OK();
}

void CloudBackendService::sync_load_for_tablets(TSyncLoadForTabletsResponse&,
                                                const TSyncLoadForTabletsRequest& request) {
    auto f = [this, tablet_ids = request.tablet_ids]() {
        std::for_each(tablet_ids.cbegin(), tablet_ids.cend(), [this](int64_t tablet_id) {
            CloudTabletSPtr tablet;
            auto result = _engine.tablet_mgr().get_tablet(tablet_id, true);
            if (!result.has_value()) {
                return;
            }
            SyncOptions options;
            options.warmup_delta_data = true;
            Status st = result.value()->sync_rowsets(options);
            if (!st.ok()) {
                LOG_WARNING("failed to sync load for tablet").error(st);
            }
        });
    };
    static_cast<void>(_engine.sync_load_for_tablets_thread_pool().submit_func(std::move(f)));
}

void CloudBackendService::get_top_n_hot_partitions(TGetTopNHotPartitionsResponse& response,
                                                   const TGetTopNHotPartitionsRequest& request) {
    _engine.tablet_hotspot().get_top_n_hot_partition(&response.hot_tables);
    response.file_cache_size = io::FileCacheFactory::instance()->get_capacity();
    response.__isset.hot_tables = !response.hot_tables.empty();
}

void CloudBackendService::warm_up_tablets(TWarmUpTabletsResponse& response,
                                          const TWarmUpTabletsRequest& request) {
    Status st;
    auto& manager = _engine.cloud_warm_up_manager();
    switch (request.type) {
    case TWarmUpTabletsRequestType::SET_JOB: {
        LOG_INFO("receive the warm up request.")
                .tag("request_type", "SET_JOB")
                .tag("job_id", request.job_id);
        st = manager.check_and_set_job_id(request.job_id);
        if (!st) {
            LOG_WARNING("SET_JOB failed.").error(st);
            break;
        }
        [[fallthrough]];
    }
    case TWarmUpTabletsRequestType::SET_BATCH: {
        LOG_INFO("receive the warm up request.")
                .tag("request_type", "SET_BATCH")
                .tag("job_id", request.job_id)
                .tag("batch_id", request.batch_id)
                .tag("jobs size", request.job_metas.size())
                .tag("tablet num of first meta",
                     request.job_metas.empty() ? 0 : request.job_metas[0].tablet_ids.size());
        bool retry = false;
        st = manager.check_and_set_batch_id(request.job_id, request.batch_id, &retry);
        if (!retry && st) {
            manager.add_job(request.job_metas);
        } else {
            if (retry) {
                LOG_WARNING("retry the job.")
                        .tag("job_id", request.job_id)
                        .tag("batch_id", request.batch_id);
            } else {
                LOG_WARNING("SET_BATCH failed.").error(st);
            }
        }
        break;
    }
    case TWarmUpTabletsRequestType::GET_CURRENT_JOB_STATE_AND_LEASE: {
        auto [job_id, batch_id, pending_job_size, finish_job_size] =
                manager.get_current_job_state();
        LOG_INFO("receive the warm up request.")
                .tag("request_type", "GET_CURRENT_JOB_STATE_AND_LEASE")
                .tag("job_id", job_id)
                .tag("batch_id", batch_id)
                .tag("pending_job_size", pending_job_size)
                .tag("finish_job_size", finish_job_size);
        response.__set_job_id(job_id);
        response.__set_batch_id(batch_id);
        response.__set_pending_job_size(pending_job_size);
        response.__set_finish_job_size(finish_job_size);
        break;
    }
    case TWarmUpTabletsRequestType::CLEAR_JOB: {
        LOG_INFO("receive the warm up request.")
                .tag("request_type", "CLEAR_JOB")
                .tag("job_id", request.job_id);
        st = manager.clear_job(request.job_id);
        break;
    }
    default:
        DCHECK(false);
    };
    st.to_thrift(&response.status);
}

void CloudBackendService::warm_up_cache_async(TWarmUpCacheAsyncResponse& response,
                                              const TWarmUpCacheAsyncRequest& request) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < request.tablet_ids.size() && i < 10; ++i) {
        if (i > 0) oss << ",";
        oss << request.tablet_ids[i];
    }
    oss << "]";
    LOG(INFO) << "warm_up_cache_async: enter, request=" << request.host << ":" << request.brpc_port
              << ", tablets num=" << request.tablet_ids.size() << ", tablet_ids=" << oss.str();

    std::string host = request.host;
    auto dns_cache = ExecEnv::GetInstance()->dns_cache();
    if (dns_cache == nullptr) {
        LOG(WARNING) << "DNS cache is not initialized, skipping hostname resolve";
    } else if (!is_valid_ip(request.host)) {
        Status status = dns_cache->get(request.host, &host);
        if (!status.ok()) {
            LOG(WARNING) << "failed to get ip from host " << request.host << ": "
                         << status.to_string();
            return;
        }
    }
    std::string brpc_addr = get_host_port(host, request.brpc_port);
    Status st = Status::OK();
    TStatus t_status;
    std::shared_ptr<PBackendService_Stub> brpc_stub =
            _exec_env->brpc_internal_client_cache()->get_new_client_no_cache(brpc_addr);
    if (!brpc_stub) {
        st = Status::RpcError("Address {} is wrong", brpc_addr);
        LOG(WARNING) << "warm_up_cache_async: failed to get brpc_stub for addr " << brpc_addr;
        return;
    }
    brpc::Controller cntl;
    PGetFileCacheMetaRequest brpc_request;
    std::for_each(request.tablet_ids.cbegin(), request.tablet_ids.cend(),
                  [&](int64_t tablet_id) { brpc_request.add_tablet_ids(tablet_id); });
    PGetFileCacheMetaResponse brpc_response;

    brpc_stub->get_file_cache_meta_by_tablet_id(&cntl, &brpc_request, &brpc_response, nullptr);
    VLOG_DEBUG << "warm_up_cache_async: request=" << brpc_request.DebugString()
               << ", response=" << brpc_response.DebugString();
    if (!cntl.Failed()) {
        _engine.file_cache_block_downloader().submit_download_task(
                std::move(*brpc_response.mutable_file_cache_block_metas()));
    } else {
        st = Status::RpcError("{} isn't connected", brpc_addr);
        LOG(WARNING) << "warm_up_cache_async: brpc call failed, addr=" << brpc_addr
                     << ", error=" << cntl.ErrorText();
    }
    st.to_thrift(&t_status);
    response.status = t_status;
}

void CloudBackendService::check_warm_up_cache_async(TCheckWarmUpCacheAsyncResponse& response,
                                                    const TCheckWarmUpCacheAsyncRequest& request) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < request.tablets.size() && i < 10; ++i) {
        if (i > 0) oss << ",";
        oss << request.tablets[i];
    }
    oss << "]";
    LOG(INFO) << "check_warm_up_cache_async: enter, request tablets num=" << request.tablets.size()
              << ", tablet_ids=" << oss.str();
    std::map<int64_t, bool> task_done;
    _engine.file_cache_block_downloader().check_download_task(request.tablets, &task_done);
    DBUG_EXECUTE_IF("CloudBackendService.check_warm_up_cache_async.return_task_false", {
        for (auto& it : task_done) {
            it.second = false;
        }
    });
    response.__set_task_done(task_done);

    for (const auto& [tablet_id, done] : task_done) {
        VLOG_DEBUG << "check_warm_up_cache_async: tablet_id=" << tablet_id << ", done=" << done;
    }

    Status st = Status::OK();
    TStatus t_status;
    st.to_thrift(&t_status);
    response.status = t_status;
}

void CloudBackendService::get_stream_load_record(TStreamLoadRecordResult& result,
                                                 int64_t last_stream_record_time) {
    BaseBackendService::get_stream_load_record(result, last_stream_record_time,
                                               _engine.get_stream_load_recorder());
}

} // namespace doris
