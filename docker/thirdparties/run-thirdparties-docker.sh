#!/bin/bash
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

################################################################
# This script will restart all thirdparty containers
################################################################

set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"

. "${ROOT}/custom_settings.env"

usage() {
    echo "
Usage: $0 <options>
  Optional options:
     [no option]        start all components
     --help,-h          show this usage
     -c mysql           start MySQL
     -c mysql,hive3     start MySQL and Hive3
     --stop             stop the specified components
     --reserve-ports    reserve host ports by setting 'net.ipv4.ip_local_reserved_ports' to avoid port already bind error
     --no-load-data     do not load data into the components

  All valid components:
    mysql,pg,oracle,sqlserver,clickhouse,es,hive2,hive3,iceberg,hudi,trino,kafka,mariadb,db2,oceanbase,lakesoul,kerberos,ranger
  "
    exit 1
}
DEFAULT_COMPONENTS="mysql,es,hive2,hive3,pg,oracle,sqlserver,clickhouse,mariadb,iceberg,db2,oceanbase,kerberos,minio"
ALL_COMPONENTS="${DEFAULT_COMPONENTS},hudi,trino,kafka,spark,lakesoul,ranger"
COMPONENTS=$2
HELP=0
STOP=0
NEED_RESERVE_PORTS=0
export NEED_LOAD_DATA=1

if ! OPTS="$(getopt \
    -n "$0" \
    -o '' \
    -l 'help' \
    -l 'stop' \
    -l 'reserve-ports' \
    -l 'no-load-data' \
    -o 'hc:' \
    -- "$@")"; then
    usage
fi

eval set -- "${OPTS}"

if [[ "$#" == 1 ]]; then
    # default
    COMPONENTS="${DEFAULT_COMPONENTS}"
else
    while true; do
        case "$1" in
        -h)
            HELP=1
            shift
            ;;
        --help)
            HELP=1
            shift
            ;;
        --stop)
            STOP=1
            shift
            ;;
        -c)
            COMPONENTS=$2
            shift 2
            ;;
        --reserve-ports)
            NEED_RESERVE_PORTS=1
            shift
            ;;
        --no-load-data)
            export NEED_LOAD_DATA=0
            shift
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Internal error"
            exit 1
            ;;
        esac
    done
    if [[ "${COMPONENTS}"x == ""x ]]; then
        if [[ "${STOP}" -eq 1 ]]; then
            COMPONENTS="${ALL_COMPONENTS}"
        fi
        if [[ "${NEED_RESERVE_PORTS}" -eq 1 ]]; then
            COMPONENTS="${DEFAULT_COMPONENTS}"
        fi
    fi
fi

if [[ "${HELP}" -eq 1 ]]; then
    usage
fi

if [[ "${COMPONENTS}"x == ""x ]]; then
    echo "Invalid arguments"
    echo ${COMPONENTS}
    usage
fi

if [[ "${CONTAINER_UID}"x == "doris--"x ]]; then
    echo "Must set CONTAINER_UID to a unique name in custom_settings.env"
    exit 1
fi

echo "Components are: ${COMPONENTS}"
echo "Container UID: ${CONTAINER_UID}"
echo "Stop: ${STOP}"

OLD_IFS="${IFS}"
IFS=','
read -r -a COMPONENTS_ARR <<<"${COMPONENTS}"
IFS="${OLD_IFS}"

RUN_MYSQL=0
RUN_PG=0
RUN_ORACLE=0
RUN_SQLSERVER=0
RUN_CLICKHOUSE=0
RUN_HIVE2=0
RUN_HIVE3=0
RUN_ES=0
RUN_ICEBERG=0
RUN_HUDI=0
RUN_TRINO=0
RUN_KAFKA=0
RUN_SPARK=0
RUN_MARIADB=0
RUN_DB2=0
RUN_OCENABASE=0
RUN_LAKESOUL=0
RUN_KERBEROS=0
RUN_MINIO=0
RUN_RANGER=0

RESERVED_PORTS="65535"

for element in "${COMPONENTS_ARR[@]}"; do
    if [[ "${element}"x == "mysql"x ]]; then
        RUN_MYSQL=1
    elif [[ "${element}"x == "pg"x ]]; then
        RUN_PG=1
    elif [[ "${element}"x == "oracle"x ]]; then
        RUN_ORACLE=1
    elif [[ "${element}"x == "sqlserver"x ]]; then
        RUN_SQLSERVER=1
    elif [[ "${element}"x == "clickhouse"x ]]; then
        RUN_CLICKHOUSE=1
    elif [[ "${element}"x == "es"x ]]; then
        RUN_ES=1
    elif [[ "${element}"x == "hive2"x ]]; then
        RUN_HIVE2=1
        RESERVED_PORTS="${RESERVED_PORTS},50070,50075" # namenode and datanode ports
    elif [[ "${element}"x == "hive3"x ]]; then
        RUN_HIVE3=1
    elif [[ "${element}"x == "kafka"x ]]; then
        RUN_KAFKA=1
    elif [[ "${element}"x == "iceberg"x ]]; then
        RUN_ICEBERG=1
    elif [[ "${element}"x == "hudi"x ]]; then
        RUN_HUDI=1
    elif [[ "${element}"x == "trino"x ]]; then
        RUN_TRINO=1
    elif [[ "${element}"x == "spark"x ]]; then
        RUN_SPARK=1
    elif [[ "${element}"x == "mariadb"x ]]; then
        RUN_MARIADB=1
    elif [[ "${element}"x == "db2"x ]]; then
        RUN_DB2=1
    elif [[ "${element}"x == "oceanbase"x ]];then
        RUN_OCEANBASE=1
    elif [[ "${element}"x == "lakesoul"x ]]; then
        RUN_LAKESOUL=1
    elif [[ "${element}"x == "kerberos"x ]]; then
        RUN_KERBEROS=1
    elif [[ "${element}"x == "minio"x ]]; then
        RUN_MINIO=1
    elif [[ "${element}"x == "ranger"x ]]; then
        RUN_RANGER=1
    else
        echo "Invalid component: ${element}"
        usage
    fi
done

reserve_ports() {
    if [[ "${NEED_RESERVE_PORTS}" -eq 0 ]]; then
        return
    fi

    if [[ "${RESERVED_PORTS}"x != ""x ]]; then
        echo "Reserve ports: ${RESERVED_PORTS}"
        sudo sysctl -w net.ipv4.ip_local_reserved_ports="${RESERVED_PORTS}"
    fi
}

start_es() {
    # elasticsearch
    cp "${ROOT}"/docker-compose/elasticsearch/es.yaml.tpl "${ROOT}"/docker-compose/elasticsearch/es.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/elasticsearch/es.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/elasticsearch/es.yaml --env-file "${ROOT}"/docker-compose/elasticsearch/es.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo mkdir -p "${ROOT}"/docker-compose/elasticsearch/data/es6/
        sudo rm -rf "${ROOT}"/docker-compose/elasticsearch/data/es6/*
        sudo mkdir -p "${ROOT}"/docker-compose/elasticsearch/data/es7/
        sudo rm -rf "${ROOT}"/docker-compose/elasticsearch/data/es7/*
        sudo mkdir -p "${ROOT}"/docker-compose/elasticsearch/data/es8/
        sudo rm -rf "${ROOT}"/docker-compose/elasticsearch/data/es8/*
        sudo chmod -R 777 "${ROOT}"/docker-compose/elasticsearch/data
        sudo mkdir -p "${ROOT}"/docker-compose/elasticsearch/logs/es6/
        sudo rm -rf "${ROOT}"/docker-compose/elasticsearch/logs/es6/*
        sudo mkdir -p "${ROOT}"/docker-compose/elasticsearch/logs/es7/
        sudo rm -rf "${ROOT}"/docker-compose/elasticsearch/logs/es7/*
        sudo mkdir -p "${ROOT}"/docker-compose/elasticsearch/logs/es8/
        sudo rm -rf "${ROOT}"/docker-compose/elasticsearch/logs/es8/*
        sudo chmod -R 777 "${ROOT}"/docker-compose/elasticsearch/logs
        sudo chmod -R 777 "${ROOT}"/docker-compose/elasticsearch/config
        sudo docker compose -f "${ROOT}"/docker-compose/elasticsearch/es.yaml --env-file "${ROOT}"/docker-compose/elasticsearch/es.env up -d --remove-orphans
    fi
}

start_mysql() {
    # mysql 5.7
    cp "${ROOT}"/docker-compose/mysql/mysql-5.7.yaml.tpl "${ROOT}"/docker-compose/mysql/mysql-5.7.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/mysql/mysql-5.7.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/mysql/mysql-5.7.yaml --env-file "${ROOT}"/docker-compose/mysql/mysql-5.7.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/mysql/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/mysql/data/
        sudo docker compose -f "${ROOT}"/docker-compose/mysql/mysql-5.7.yaml --env-file "${ROOT}"/docker-compose/mysql/mysql-5.7.env up -d --wait
    fi
}

start_pg() {
    # pg 14
    cp "${ROOT}"/docker-compose/postgresql/postgresql-14.yaml.tpl "${ROOT}"/docker-compose/postgresql/postgresql-14.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/postgresql/postgresql-14.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/postgresql/postgresql-14.yaml --env-file "${ROOT}"/docker-compose/postgresql/postgresql-14.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/postgresql/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/postgresql/data/data
        sudo docker compose -f "${ROOT}"/docker-compose/postgresql/postgresql-14.yaml --env-file "${ROOT}"/docker-compose/postgresql/postgresql-14.env up -d --wait
    fi
}

start_oracle() {
    # oracle
    cp "${ROOT}"/docker-compose/oracle/oracle-11.yaml.tpl "${ROOT}"/docker-compose/oracle/oracle-11.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/oracle/oracle-11.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/oracle/oracle-11.yaml --env-file "${ROOT}"/docker-compose/oracle/oracle-11.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/oracle/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/oracle/data/
        sudo docker compose -f "${ROOT}"/docker-compose/oracle/oracle-11.yaml --env-file "${ROOT}"/docker-compose/oracle/oracle-11.env up -d --wait
    fi
}

start_db2() {
    # db2
    cp "${ROOT}"/docker-compose/db2/db2.yaml.tpl "${ROOT}"/docker-compose/db2/db2.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/db2/db2.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/db2/db2.yaml --env-file "${ROOT}"/docker-compose/db2/db2.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/db2/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/db2/data/
        sudo docker compose -f "${ROOT}"/docker-compose/db2/db2.yaml --env-file "${ROOT}"/docker-compose/db2/db2.env up -d --wait
    fi
}

start_oceanbase() {
    # oceanbase
    cp "${ROOT}"/docker-compose/oceanbase/oceanbase.yaml.tpl "${ROOT}"/docker-compose/oceanbase/oceanbase.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/oceanbase/oceanbase.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/oceanbase/oceanbase.yaml --env-file "${ROOT}"/docker-compose/oceanbase/oceanbase.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/oceanbase/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/oceanbase/data/
        sudo docker compose -f "${ROOT}"/docker-compose/oceanbase/oceanbase.yaml --env-file "${ROOT}"/docker-compose/oceanbase/oceanbase.env up -d --wait
    fi
}

start_sqlserver() {
    # sqlserver
    cp "${ROOT}"/docker-compose/sqlserver/sqlserver.yaml.tpl "${ROOT}"/docker-compose/sqlserver/sqlserver.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/sqlserver/sqlserver.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/sqlserver/sqlserver.yaml --env-file "${ROOT}"/docker-compose/sqlserver/sqlserver.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/sqlserver/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/sqlserver/data/
        sudo docker compose -f "${ROOT}"/docker-compose/sqlserver/sqlserver.yaml --env-file "${ROOT}"/docker-compose/sqlserver/sqlserver.env up -d --wait
    fi
}

start_clickhouse() {
    # clickhouse
    cp "${ROOT}"/docker-compose/clickhouse/clickhouse.yaml.tpl "${ROOT}"/docker-compose/clickhouse/clickhouse.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/clickhouse/clickhouse.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/clickhouse/clickhouse.yaml --env-file "${ROOT}"/docker-compose/clickhouse/clickhouse.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm "${ROOT}"/docker-compose/clickhouse/data/* -rf
        sudo mkdir -p "${ROOT}"/docker-compose/clickhouse/data/
        sudo docker compose -f "${ROOT}"/docker-compose/clickhouse/clickhouse.yaml --env-file "${ROOT}"/docker-compose/clickhouse/clickhouse.env up -d --wait
    fi
}

start_kafka() {
    # kafka
    KAFKA_CONTAINER_ID="${CONTAINER_UID}kafka"
    eth_name=$(ifconfig -a | grep -E "^eth[0-9]" | sort -k1.4n | awk -F ':' '{print $1}' | head -n 1)
    IP_HOST=$(ifconfig "${eth_name}" | grep inet | grep -v 127.0.0.1 | grep -v inet6 | awk '{print $2}' | tr -d "addr:" | head -n 1)
    cp "${ROOT}"/docker-compose/kafka/kafka.yaml.tpl "${ROOT}"/docker-compose/kafka/kafka.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/kafka/kafka.yaml
    sed -i "s/localhost/${IP_HOST}/g" "${ROOT}"/docker-compose/kafka/kafka.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/kafka/kafka.yaml --env-file "${ROOT}"/docker-compose/kafka/kafka.env down

    create_kafka_topics() {
        local container_id="$1"
        local ip_host="$2"
        local backup_dir=/home/work/pipline/backup_center

        declare -a topics=("basic_data" "basic_array_data" "basic_data_with_errors" "basic_array_data_with_errors" "basic_data_timezone" "basic_array_data_timezone" "trino_kafka_basic_data")

        for topic in "${topics[@]}"; do
            echo "docker exec "${container_id}" bash -c echo '/opt/bitnami/kafka/bin/kafka-topics.sh --create --bootstrap-server '${ip_host}:19193' --topic '${topic}'"
            docker exec "${container_id}" bash -c "/opt/bitnami/kafka/bin/kafka-topics.sh --create --bootstrap-server '${ip_host}:19193' --topic '${topic}'"
        done

    }

    if [[ "${STOP}" -ne 1 ]]; then
        sudo docker compose -f "${ROOT}"/docker-compose/kafka/kafka.yaml --env-file "${ROOT}"/docker-compose/kafka/kafka.env up --build --remove-orphans -d
        sleep 10s
        create_kafka_topics "${KAFKA_CONTAINER_ID}" "${IP_HOST}"
    fi
}

start_hive2() {
    # hive2
    # If the doris cluster you need to test is single-node, you can use the default values; If the doris cluster you need to test is composed of multiple nodes, then you need to set the IP_HOST according to the actual situation of your machine
    #default value
    IP_HOST="127.0.0.1"
    eth_name=$(ifconfig -a | grep -E "^eth[0-9]" | sort -k1.4n | awk -F ':' '{print $1}' | head -n 1)
    IP_HOST=$(ifconfig "${eth_name}" | grep inet | grep -v 127.0.0.1 | grep -v inet6 | awk '{print $2}' | tr -d "addr:" | head -n 1)

    if [ "_${IP_HOST}" == "_" ]; then
        echo "please set IP_HOST according to your actual situation"
        exit -1
    fi
    # before start it, you need to download parquet file package, see "README" in "docker-compose/hive/scripts/"
    sed -i "s/s3Endpoint/${s3Endpoint}/g" "${ROOT}"/docker-compose/hive/scripts/hive-metastore.sh
    sed -i "s/s3BucketName/${s3BucketName}/g" "${ROOT}"/docker-compose/hive/scripts/hive-metastore.sh

    # sed all s3 info in run.sh of each suite
    find "${ROOT}/docker-compose/hive/scripts/data/" -type f -name "run.sh" | while read -r file; do
        if [ -f "$file" ]; then
            echo "Processing $file"
            sed -i "s/s3Endpoint/${s3Endpoint}/g" "${file}"
            sed -i "s/s3BucketName/${s3BucketName}/g" "${file}"
        else
            echo "File not found: $file"
        fi
    done

    # generate hive-2x.yaml
    export IP_HOST=${IP_HOST}
    export CONTAINER_UID=${CONTAINER_UID}
    . "${ROOT}"/docker-compose/hive/hive-2x_settings.env
    envsubst <"${ROOT}"/docker-compose/hive/hive-2x.yaml.tpl >"${ROOT}"/docker-compose/hive/hive-2x.yaml
    envsubst <"${ROOT}"/docker-compose/hive/hadoop-hive.env.tpl >"${ROOT}"/docker-compose/hive/hadoop-hive-2x.env
    envsubst <"${ROOT}"/docker-compose/hive/hadoop-hive-2x.env.tpl >> "${ROOT}"/docker-compose/hive/hadoop-hive-2x.env
    sudo docker compose -p ${CONTAINER_UID}hive2 -f "${ROOT}"/docker-compose/hive/hive-2x.yaml --env-file "${ROOT}"/docker-compose/hive/hadoop-hive-2x.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo docker compose -p ${CONTAINER_UID}hive2 -f "${ROOT}"/docker-compose/hive/hive-2x.yaml --env-file "${ROOT}"/docker-compose/hive/hadoop-hive-2x.env up --build --remove-orphans -d --wait
    fi
}

start_hive3() {
    # hive3
    # If the doris cluster you need to test is single-node, you can use the default values; If the doris cluster you need to test is composed of multiple nodes, then you need to set the IP_HOST according to the actual situation of your machine
    #default value
    IP_HOST="127.0.0.1"
    eth_name=$(ifconfig -a | grep -E "^eth[0-9]" | sort -k1.4n | awk -F ':' '{print $1}' | head -n 1)
    IP_HOST=$(ifconfig "${eth_name}" | grep inet | grep -v 127.0.0.1 | grep -v inet6 | awk '{print $2}' | tr -d "addr:" | head -n 1)
    if [ "_${IP_HOST}" == "_" ]; then
        echo "please set IP_HOST according to your actual situation"
        exit -1
    fi
    # before start it, you need to download parquet file package, see "README" in "docker-compose/hive/scripts/"
    sed -i "s/s3Endpoint/${s3Endpoint}/g" "${ROOT}"/docker-compose/hive/scripts/hive-metastore.sh
    sed -i "s/s3BucketName/${s3BucketName}/g" "${ROOT}"/docker-compose/hive/scripts/hive-metastore.sh

    # sed all s3 info in run.sh of each suite
    find "${ROOT}/docker-compose/hive/scripts/data" -type f -name "run.sh" | while read -r file; do
        if [ -f "$file" ]; then
            echo "Processing $file"
            sed -i "s/s3Endpoint/${s3Endpoint}/g" "${file}"
            sed -i "s/s3BucketName/${s3BucketName}/g" "${file}"
        else
            echo "File not found: $file"
        fi
    done

    # generate hive-3x.yaml
    export IP_HOST=${IP_HOST}
    export CONTAINER_UID=${CONTAINER_UID}
    . "${ROOT}"/docker-compose/hive/hive-3x_settings.env
    envsubst <"${ROOT}"/docker-compose/hive/hive-3x.yaml.tpl >"${ROOT}"/docker-compose/hive/hive-3x.yaml
    envsubst <"${ROOT}"/docker-compose/hive/hadoop-hive.env.tpl >"${ROOT}"/docker-compose/hive/hadoop-hive-3x.env
    envsubst <"${ROOT}"/docker-compose/hive/hadoop-hive-3x.env.tpl >> "${ROOT}"/docker-compose/hive/hadoop-hive-3x.env
    sudo docker compose -p ${CONTAINER_UID}hive3 -f "${ROOT}"/docker-compose/hive/hive-3x.yaml --env-file "${ROOT}"/docker-compose/hive/hadoop-hive-3x.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo docker compose -p ${CONTAINER_UID}hive3 -f "${ROOT}"/docker-compose/hive/hive-3x.yaml --env-file "${ROOT}"/docker-compose/hive/hadoop-hive-3x.env up --build --remove-orphans -d --wait
    fi
}

start_spark() {
    sudo docker compose -f "${ROOT}"/docker-compose/spark/spark.yaml down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo docker compose -f "${ROOT}"/docker-compose/spark/spark.yaml up --build --remove-orphans -d
    fi
}

start_iceberg() {
    # iceberg
    ICEBERG_DIR=${ROOT}/docker-compose/iceberg
    cp "${ROOT}"/docker-compose/iceberg/iceberg.yaml.tpl "${ROOT}"/docker-compose/iceberg/iceberg.yaml
    cp "${ROOT}"/docker-compose/iceberg/entrypoint.sh.tpl "${ROOT}"/docker-compose/iceberg/entrypoint.sh
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/iceberg/iceberg.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/iceberg/entrypoint.sh
    cp "${ROOT}"/docker-compose/iceberg/entrypoint.sh "${ROOT}"/docker-compose/iceberg/scripts/entrypoint.sh
    sudo docker compose -f "${ROOT}"/docker-compose/iceberg/iceberg.yaml --env-file "${ROOT}"/docker-compose/iceberg/iceberg.env down
    if [[ "${STOP}" -ne 1 ]]; then
        if [[ ! -d "${ICEBERG_DIR}/data" ]]; then
            echo "${ICEBERG_DIR}/data does not exist"
            cd "${ICEBERG_DIR}" \
            && rm -f iceberg_data*.zip \
            && wget -P "${ROOT}"/docker-compose/iceberg https://"${s3BucketName}.${s3Endpoint}"/regression/datalake/pipeline_data/iceberg_data_paimon_101.zip \
            && sudo unzip iceberg_data_paimon_101.zip \
            && sudo mv iceberg_data data \
            && sudo rm -rf iceberg_data_paimon_101.zip
            cd -
        else
            echo "${ICEBERG_DIR}/data exist, continue !"
        fi

        sudo docker compose -f "${ROOT}"/docker-compose/iceberg/iceberg.yaml --env-file "${ROOT}"/docker-compose/iceberg/iceberg.env up -d --wait
    fi
}

start_hudi() {
    # hudi
    cp "${ROOT}"/docker-compose/hudi/hudi.yaml.tpl "${ROOT}"/docker-compose/hudi/hudi.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/hudi/hudi.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/hudi/hudi.yaml --env-file "${ROOT}"/docker-compose/hudi/hadoop.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo rm -rf "${ROOT}"/docker-compose/hudi/historyserver
        sudo mkdir "${ROOT}"/docker-compose/hudi/historyserver
        sudo rm -rf "${ROOT}"/docker-compose/hudi/hive-metastore-postgresql
        sudo mkdir "${ROOT}"/docker-compose/hudi/hive-metastore-postgresql
        if [[ ! -d "${ROOT}/docker-compose/hudi/scripts/hudi_docker_compose_attached_file" ]]; then
            echo "Attached files does not exist, please download the https://doris-regression-hk.oss-cn-hongkong.aliyuncs.com/regression/load/hudi/hudi_docker_compose_attached_file.zip file to the docker-compose/hudi/scripts/ directory and unzip it."
            exit 1
        fi
        sudo docker compose -f "${ROOT}"/docker-compose/hudi/hudi.yaml --env-file "${ROOT}"/docker-compose/hudi/hadoop.env up -d
        echo "sleep 15, wait server start"
        sleep 15
        docker exec -it adhoc-1 /bin/bash /var/scripts/setup_demo_container_adhoc_1.sh
        docker exec -it adhoc-2 /bin/bash /var/scripts/setup_demo_container_adhoc_2.sh
    fi
}

start_trino() {
    # trino
    trino_docker="${ROOT}"/docker-compose/trino
    TRINO_CONTAINER_ID="${CONTAINER_UID}trino"
    NAMENODE_CONTAINER_ID="${CONTAINER_UID}namenode"
    HIVE_METASTORE_CONTAINER_ID=${CONTAINER_UID}hive-metastore
    for file in trino_hive.yaml trino_hive.env gen_env.sh hive.properties; do
        cp "${trino_docker}/$file.tpl" "${trino_docker}/$file"
        if [[ $file != "hive.properties" ]]; then
            sed -i "s/doris--/${CONTAINER_UID}/g" "${trino_docker}/$file"
        fi
    done

    bash "${trino_docker}"/gen_env.sh
    sudo docker compose -f "${trino_docker}"/trino_hive.yaml --env-file "${trino_docker}"/trino_hive.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo sed -i "/${NAMENODE_CONTAINER_ID}/d" /etc/hosts
        sudo docker compose -f "${trino_docker}"/trino_hive.yaml --env-file "${trino_docker}"/trino_hive.env up --build --remove-orphans -d
        sudo echo "127.0.0.1 ${NAMENODE_CONTAINER_ID}" >>/etc/hosts
        sleep 20s
        hive_metastore_ip=$(docker inspect --format='{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' ${HIVE_METASTORE_CONTAINER_ID})

        if [ -z "$hive_metastore_ip" ]; then
            echo "Failed to get Hive Metastore IP address" >&2
            exit 1
        else
            echo "Hive Metastore IP address is: $hive_metastore_ip"
        fi

        sed -i "s/metastore_ip/${hive_metastore_ip}/g" "${trino_docker}"/hive.properties
        docker cp "${trino_docker}"/hive.properties "${CONTAINER_UID}trino":/etc/trino/catalog/

        # trino load hive catalog need restart server
        max_retries=3

        function control_container() {
            max_retries=3
            operation=$1
            expected_status=$2
            retries=0

            while [ $retries -lt $max_retries ]; do
                status=$(docker inspect --format '{{.State.Running}}' ${TRINO_CONTAINER_ID})
                if [ "${status}" == "${expected_status}" ]; then
                    echo "Container ${TRINO_CONTAINER_ID} has ${operation}ed successfully."
                    break
                else
                    echo "Waiting for container ${TRINO_CONTAINER_ID} to ${operation}..."
                    sleep 5s
                    ((retries++))
                fi
                sleep 3s
            done

            if [ $retries -eq $max_retries ]; then
                echo "${operation} operation failed to complete after $max_retries attempts."
                exit 1
            fi
        }
        # Stop the container
        docker stop ${TRINO_CONTAINER_ID}
        sleep 5s
        control_container "stop" "false"

        # Start the container
        docker start ${TRINO_CONTAINER_ID}
        control_container "start" "true"

        # waite trino init
        sleep 20s
        # execute create table sql
        docker exec -it ${TRINO_CONTAINER_ID} /bin/bash -c 'trino -f /scripts/create_trino_table.sql'
    fi
}

start_mariadb() {
    # mariadb
    cp "${ROOT}"/docker-compose/mariadb/mariadb-10.yaml.tpl "${ROOT}"/docker-compose/mariadb/mariadb-10.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/mariadb/mariadb-10.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/mariadb/mariadb-10.yaml --env-file "${ROOT}"/docker-compose/mariadb/mariadb-10.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo mkdir -p "${ROOT}"/docker-compose/mariadb/data/
        sudo rm "${ROOT}"/docker-compose/mariadb/data/* -rf
        sudo docker compose -f "${ROOT}"/docker-compose/mariadb/mariadb-10.yaml --env-file "${ROOT}"/docker-compose/mariadb/mariadb-10.env up -d --wait
    fi
}

start_lakesoul() {
    echo "RUN_LAKESOUL"
    cp "${ROOT}"/docker-compose/lakesoul/lakesoul.yaml.tpl "${ROOT}"/docker-compose/lakesoul/lakesoul.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/lakesoul/lakesoul.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/lakesoul/lakesoul.yaml down
    sudo rm -rf "${ROOT}"/docker-compose/lakesoul/data
    if [[ "${STOP}" -ne 1 ]]; then
        echo "PREPARE_LAKESOUL_DATA"
        sudo docker compose -f "${ROOT}"/docker-compose/lakesoul/lakesoul.yaml up -d
        ## import tpch data into lakesoul
        ## install rustup
        curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --default-toolchain none -y
        # shellcheck source=/dev/null
        . "${HOME}/.cargo/env"
        ## install rust nightly-2023-05-20
        rustup install nightly-2023-05-20
        ## download&generate tpch data
        mkdir -p lakesoul/test_files/tpch/data
        git clone https://github.com/databricks/tpch-dbgen.git
        cd tpch-dbgen
        make
        ./dbgen -f -s 0.1
        mv *.tbl ../lakesoul/test_files/tpch/data
        cd ..
        export TPCH_DATA=$(realpath lakesoul/test_files/tpch/data)
        ## import tpch data
        git clone https://github.com/lakesoul-io/LakeSoul.git
        #    git checkout doris_dev
        cd LakeSoul/rust
        cargo test load_tpch_data --package lakesoul-datafusion --features=ci -- --nocapture
    fi
}

start_kerberos() {
    echo "RUN_KERBEROS"
    eth_name=$(ifconfig -a | grep -E "^eth[0-9]" | sort -k1.4n | awk -F ':' '{print $1}' | head -n 1)
    IP_HOST=$(ifconfig "${eth_name}" | grep inet | grep -v 127.0.0.1 | grep -v inet6 | awk '{print $2}' | tr -d "addr:" | head -n 1)
    export IP_HOST=${IP_HOST}
    export CONTAINER_UID=${CONTAINER_UID}
    envsubst <"${ROOT}"/docker-compose/kerberos/kerberos.yaml.tpl >"${ROOT}"/docker-compose/kerberos/kerberos.yaml
    for i in {1..2}; do
        . "${ROOT}"/docker-compose/kerberos/kerberos${i}_settings.env
        envsubst <"${ROOT}"/docker-compose/kerberos/hadoop-hive.env.tpl >"${ROOT}"/docker-compose/kerberos/hadoop-hive-${i}.env
        envsubst <"${ROOT}"/docker-compose/kerberos/conf/my.cnf.tpl > "${ROOT}"/docker-compose/kerberos/conf/kerberos${i}/my.cnf
        envsubst <"${ROOT}"/docker-compose/kerberos/conf/kerberos${i}/kdc.conf.tpl > "${ROOT}"/docker-compose/kerberos/conf/kerberos${i}/kdc.conf
        envsubst <"${ROOT}"/docker-compose/kerberos/conf/kerberos${i}/krb5.conf.tpl > "${ROOT}"/docker-compose/kerberos/conf/kerberos${i}/krb5.conf
    done
    sudo chmod a+w /etc/hosts
    sudo sed -i "1i${IP_HOST} hadoop-master" /etc/hosts
    sudo sed -i "1i${IP_HOST} hadoop-master-2" /etc/hosts
    sudo docker compose -f "${ROOT}"/docker-compose/kerberos/kerberos.yaml down
    sudo rm -rf "${ROOT}"/docker-compose/kerberos/data
    if [[ "${STOP}" -ne 1 ]]; then
        echo "PREPARE KERBEROS DATA"
        rm -rf "${ROOT}"/docker-compose/kerberos/two-kerberos-hives/*.keytab
        rm -rf "${ROOT}"/docker-compose/kerberos/two-kerberos-hives/*.jks
        rm -rf "${ROOT}"/docker-compose/kerberos/two-kerberos-hives/*.conf
        sudo docker compose -f "${ROOT}"/docker-compose/kerberos/kerberos.yaml up --remove-orphans --wait -d
        sudo rm -f /keytabs
        sudo ln -s "${ROOT}"/docker-compose/kerberos/two-kerberos-hives /keytabs
        sudo cp "${ROOT}"/docker-compose/kerberos/common/conf/doris-krb5.conf /keytabs/krb5.conf
        sudo cp "${ROOT}"/docker-compose/kerberos/common/conf/doris-krb5.conf /etc/krb5.conf
        sleep 2
    fi
}

start_minio() {
    echo "RUN_MINIO"
    cp "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.yaml.tpl "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.yaml
    sed -i "s/doris--/${CONTAINER_UID}/g" "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.yaml --env-file "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo docker compose -f "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.yaml --env-file "${ROOT}"/docker-compose/minio/minio-RELEASE.2024-11-07.env up -d
    fi
}

start_ranger() {
    echo "RUN_RANGER"
    export CONTAINER_UID=${CONTAINER_UID}
    find "${ROOT}/docker-compose/ranger/script" -type f -exec sed -i "s/s3Endpoint/${s3Endpoint}/g" {} \;
    find "${ROOT}/docker-compose/ranger/script" -type f -exec sed -i "s/s3BucketName/${s3BucketName}/g" {} \;
    . "${ROOT}/docker-compose/ranger/ranger_settings.env"
    envsubst <"${ROOT}"/docker-compose/ranger/ranger.yaml.tpl >"${ROOT}"/docker-compose/ranger/ranger.yaml
    sudo docker compose -f "${ROOT}"/docker-compose/ranger/ranger.yaml --env-file "${ROOT}"/docker-compose/ranger/ranger_settings.env down
    if [[ "${STOP}" -ne 1 ]]; then
        sudo docker compose -f "${ROOT}"/docker-compose/ranger/ranger.yaml --env-file "${ROOT}"/docker-compose/ranger/ranger_settings.env up -d --wait --remove-orphans
    fi
}

echo "starting dockers in parallel"

reserve_ports

declare -A pids

if [[ "${RUN_ES}" -eq 1 ]]; then
    start_es > start_es.log  2>&1 &
    pids["es"]=$!
fi

if [[ "${RUN_MYSQL}" -eq 1 ]]; then
    start_mysql > start_mysql.log 2>&1 &
    pids["mysql"]=$!
fi

if [[ "${RUN_PG}" -eq 1 ]]; then
    start_pg > start_pg.log 2>&1 &
    pids["pg"]=$!
fi

if [[ "${RUN_ORACLE}" -eq 1 ]]; then
    start_oracle > start_oracle.log 2>&1 &
    pids["oracle"]=$!
fi

if [[ "${RUN_DB2}" -eq 1 ]]; then
    start_db2 > start_db2.log 2>&1 &
    pids["db2"]=$!
fi

if [[ "${RUN_OCEANBASE}" -eq 1 ]]; then
    start_oceanbase > start_oceanbase.log 2>&1 &
    pids["oceanbase"]=$!
fi

if [[ "${RUN_SQLSERVER}" -eq 1 ]]; then
    start_sqlserver > start_sqlserver.log 2>&1 &
    pids["sqlserver"]=$!
fi

if [[ "${RUN_CLICKHOUSE}" -eq 1 ]]; then
    start_clickhouse > start_clickhouse.log 2>&1 &
    pids["clickhouse"]=$!
fi

if [[ "${RUN_KAFKA}" -eq 1 ]]; then
    start_kafka > start_kafka.log 2>&1 &
    pids["kafka"]=$!
fi

if [[ "${RUN_HIVE2}" -eq 1 ]]; then
    start_hive2 > start_hive2.log 2>&1 &
    pids["hive2"]=$!
fi

if [[ "${RUN_HIVE3}" -eq 1 ]]; then
    start_hive3 > start_hive3.log 2>&1 &
    pids["hive3"]=$!
fi

if [[ "${RUN_SPARK}" -eq 1 ]]; then
    start_spark > start_spark.log 2>&1 &
    pids["spark"]=$!
fi

if [[ "${RUN_ICEBERG}" -eq 1 ]]; then
    start_iceberg > start_iceberg.log 2>&1 &
    pids["iceberg"]=$!
fi

if [[ "${RUN_HUDI}" -eq 1 ]]; then
    start_hudi > start_hudi.log 2>&1 &
    pids["hudi"]=$!
fi

if [[ "${RUN_TRINO}" -eq 1 ]]; then
    start_trino > start_trino.log 2>&1 &
    pids["trino"]=$!
fi

if [[ "${RUN_MARIADB}" -eq 1 ]]; then
    start_mariadb > start_mariadb.log 2>&1 &
    pids["mariadb"]=$!
fi

if [[ "${RUN_LAKESOUL}" -eq 1 ]]; then
    start_lakesoul > start_lakesoule.log 2>&1 &
    pids["lakesoul"]=$!
fi

if [[ "${RUN_MINIO}" -eq 1 ]]; then
    start_minio > start_minio.log 2>&1 &
    pids["minio"]=$!
fi

if [[ "${RUN_KERBEROS}" -eq 1 ]]; then
    start_kerberos > start_kerberos.log 2>&1 &
    pids["kerberos"]=$!
fi

if [[ "${RUN_RANGER}" -eq 1 ]]; then
    start_ranger > start_ranger.log 2>&1 &
    pids["ranger"]=$!
fi

echo "waiting all dockers starting done"

for compose in "${!pids[@]}"; do
    # prevent wait return 1 make the script exit
    status=0
    wait "${pids[$compose]}" || status=$?
    if [ $status -ne 0 ] && [ $compose != "db2" ]; then
        echo "docker $compose started failed with status $status"
        echo "print start_${compose}.log"
        cat start_${compose}.log || true

        echo ""
        echo "print last 100 logs of the latest unhealthy container"
        sudo docker ps -a --latest --filter 'health=unhealthy' --format '{{.ID}}' | xargs -I '{}' sh -c 'echo "=== Logs of {} ===" && docker logs -t --tail 100 "{}"'

        exit 1
    fi
done

echo "docker started"
sudo docker ps -a --format "{{.ID}} | {{.Image}} | {{.Status}}"
echo "all dockers started successfully"
