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

package org.apache.doris.nereids.jobs.rewrite;

import org.apache.doris.nereids.jobs.JobContext;
import org.apache.doris.nereids.rules.Rule;

import java.util.List;

/** AdaptiveBottomUpRewriteJob */
public class AdaptiveBottomUpRewriteJob implements RewriteJob {
    private BottomUpVisitorRewriteJob visitorJob;
    private RootPlanTreeRewriteJob stackJob;

    public AdaptiveBottomUpRewriteJob(BottomUpVisitorRewriteJob visitorJob, RootPlanTreeRewriteJob stackJob) {
        this.visitorJob = visitorJob;
        this.stackJob = stackJob;
    }

    @Override
    public void execute(JobContext jobContext) {
        int depth = jobContext.getCascadesContext().getRewritePlan().depth();
        int depthThreshold = jobContext.getCascadesContext().getConnectContext()
                .getSessionVariable().enableVisitorRewriterDepthThreshold;
        if (depth <= depthThreshold) {
            visitorJob.execute(jobContext);
        } else {
            stackJob.execute(jobContext);
        }
    }

    @Override
    public boolean isOnce() {
        return visitorJob.isOnce();
    }

    public BottomUpVisitorRewriteJob getVisitorJob() {
        return visitorJob;
    }

    public RootPlanTreeRewriteJob getStackJob() {
        return stackJob;
    }

    public List<Rule> getRules() {
        return stackJob.getRules();
    }
}
