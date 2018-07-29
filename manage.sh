
#! /bin/bash

CurDir="$(cd "$(dirname "${BASH_SOURCE[0]}" )" && pwd)"

STP_CI_IMAGE=registry.cn-hangzhou.aliyuncs.com/leosocy/stp:ci
STP_CONTAINER_NAME=stp
BUILD_DIR=build
TEST_NAME=stp_test

###########################
# Google cpplint settings #
###########################
CPP_LINT_IMAGE=registry.cn-hangzhou.aliyuncs.com/leosocy/cpplint
CPP_LINT_CONTAINER_NAME=cpplint

error_return() {
    echo "[ERROR] $1"
    exit 1
}

check_exec_success() {
    if [ "$1" != "0" ]; then
        echo "[ERROR] $2 failed!"
        exit 1
    else
        echo "[INFO] $2 success!"
    fi
}

updateimages() {
    docekr pull ${STP_CI_IMAGE}
}

runtest() {
    docker stop ${STP_CONTAINER_NAME} 2>/dev/null
    docker rm -v ${STP_CONTAINER_NAME} 2>/dev/null
    docker run -it --rm --name ${STP_CONTAINER_NAME} \
        -v ${CurDir}:/home/stp -w /home/stp \
        ${STP_CI_IMAGE} sh -c " \
            mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR} \
            && cmake ../tests && make -j build_and_test
        "
}

gdbtest() {
    docker stop ${STP_CONTAINER_NAME} 2>/dev/null
    docker rm -v ${STP_CONTAINER_NAME} 2>/dev/null
    docker run -it --rm --name ${STP_CONTAINER_NAME} \
        --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
        -v ${CurDir}:/home/stp -w /home/stp \
        ${STP_CI_IMAGE} sh -c " \
            mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR} \
            && cmake ../tests && make -j && gdb ${TEST_NAME}
        "
}

cpplint() {
    docker stop ${CPP_LINT_CONTAINER_NAME} 2>/dev/null
    docker rm -v ${CPP_LINT_CONTAINER_NAME} 2>/dev/null
    docker run -it --rm --name ${CPP_LINT_CONTAINER_NAME} \
        -v ${CurDir}:/home/stp -w /home/stp \
        ${CPP_LINT_IMAGE} sh -c " \
            cpplint --linelength=120 include/smart_thread_pool.h
        "
}

################
# script start #
################

case "$1" in
    runtest) runtest ;;
    gdbtest) gdbtest ;;
    cpplint) cpplint ;;
    updateimages) updateimages ;;
    *)
        echo "Usage:"
        echo "./manage.sh runtest|gdbtest"
        echo "./manage.sh cpplint"
        echo "./manage.sh updateimages"
        exit 1
        ;;
esac

exit 0
