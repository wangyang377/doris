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

import org.apache.kafka.clients.admin.AdminClient
import org.apache.kafka.clients.producer.KafkaProducer
import org.apache.kafka.clients.producer.ProducerRecord
import org.apache.kafka.clients.producer.ProducerConfig
import org.codehaus.groovy.runtime.IOGroovyMethods

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

import org.apache.kafka.clients.admin.AdminClient
import org.apache.kafka.clients.producer.KafkaProducer
import org.apache.kafka.clients.producer.ProducerRecord
import org.apache.kafka.clients.producer.ProducerConfig

suite("test_multi_table_load_eror","nonConcurrent") {
    def kafkaCsvTpoics = [
                  "multi_table_csv",
                ]

    String enabled = context.config.otherConfigs.get("enableKafkaTest")
    String kafka_port = context.config.otherConfigs.get("kafka_port")
    String externalEnvIp = context.config.otherConfigs.get("externalEnvIp")
    def kafka_broker = "${externalEnvIp}:${kafka_port}"

    if (enabled != null && enabled.equalsIgnoreCase("true")) {
        // define kafka 
        def props = new Properties()
        props.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG, "${kafka_broker}".toString())
        props.put(ProducerConfig.KEY_SERIALIZER_CLASS_CONFIG, "org.apache.kafka.common.serialization.StringSerializer")
        props.put(ProducerConfig.VALUE_SERIALIZER_CLASS_CONFIG, "org.apache.kafka.common.serialization.StringSerializer")
        // add timeout config
        props.put(ProducerConfig.MAX_BLOCK_MS_CONFIG, "10000")  
        props.put(ProducerConfig.REQUEST_TIMEOUT_MS_CONFIG, "10000")

        // check conenction
        def verifyKafkaConnection = { prod ->
            try {
                logger.info("=====try to connect Kafka========")
                def partitions = prod.partitionsFor("__connection_verification_topic")
                return partitions != null
            } catch (Exception e) {
                throw new Exception("Kafka connect fail: ${e.message}".toString())
            }
        }
        // Create kafka producer
        def producer = new KafkaProducer<>(props)
        try {
            logger.info("Kafka connecting: ${kafka_broker}")
            if (!verifyKafkaConnection(producer)) {
                throw new Exception("can't get any kafka info")
            }
        } catch (Exception e) {
            logger.error("FATAL: " + e.getMessage())
            producer.close()
            throw e  
        }
        logger.info("Kafka connect success")

        for (String kafkaCsvTopic in kafkaCsvTpoics) {
            def txt = new File("""${context.file.parent}/data/${kafkaCsvTopic}.csv""").text
            def lines = txt.readLines()
            lines.each { line ->
                logger.info("=====${line}========")
                def record = new ProducerRecord<>(kafkaCsvTopic, null, line)
                producer.send(record)
            }
        }
    }

    def load_with_injection = { injection ->
        def jobName = "test_multi_table_load_eror"
        def tableName = "dup_tbl_basic_multi_table"
        if (enabled != null && enabled.equalsIgnoreCase("true")) {
            try {
                GetDebugPoint().enableDebugPointForAllBEs(injection)
                sql new File("""${context.file.parent}/ddl/${tableName}_drop.sql""").text
                sql new File("""${context.file.parent}/ddl/${tableName}_create.sql""").text
                sql "sync"

                sql """
                    CREATE ROUTINE LOAD ${jobName}
                    COLUMNS TERMINATED BY "|"
                    PROPERTIES
                    (
                        "max_batch_interval" = "5",
                        "max_batch_rows" = "300000",
                        "max_batch_size" = "209715200"
                    )
                    FROM KAFKA
                    (
                        "kafka_broker_list" = "${externalEnvIp}:${kafka_port}",
                        "kafka_topic" = "multi_table_csv",
                        "property.kafka_default_offsets" = "OFFSET_BEGINNING"
                    );
                """
                sql "sync"

                def count = 0
                while (true) {
                    sleep(1000)
                    def res = sql "show routine load for ${jobName}"
                    def state = res[0][8].toString()
                    if (state != "RUNNING") {
                        count++
                        if (count > 60) {
                            assertEquals(1, 2)
                        } 
                        continue;
                    }
                    log.info("reason of state changed: ${res[0][17].toString()}".toString())
                    break;
                }

                count = 0
                while (true) {
                    sleep(1000)
                    def res = sql "show routine load for ${jobName}"
                    def state = res[0][8].toString()
                    if (state == "RUNNING") {
                        count++
                        if (count > 60) {
                            break;
                        }
                        continue;
                    }
                    log.info("reason of state changed: ${res[0][17].toString()}".toString())
                    assertEquals(1, 2)
                }

                count = 0
                while (true) {
                    sleep(1000)
                    def res = sql "show routine load for ${jobName}"
                    log.info("routine load statistic: ${res[0][14].toString()}".toString())
                    def json = parseJson(res[0][14])
                    if (json.loadedRows.toString() == "0") {
                        count++
                        if (count > 60) {
                            assertEquals(1, 2)
                        } 
                        continue;
                    }
                    break;
                }
            } finally {
                GetDebugPoint().disableDebugPointForAllBEs(injection)
                sql "stop routine load for ${jobName}"
                sql "DROP TABLE IF EXISTS ${tableName}"
            }
        }
    }

    load_with_injection("FragmentMgr.exec_plan_fragment.failed")
    load_with_injection("MultiTablePipe.exec_plans.failed")
}