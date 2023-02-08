#!/bin/bash -uex

# NOTE: not loger used: tftp-hpa 5.2
# Usage: tftp [-4][-6][-v][-V][-l][-m mode][-w size][-B blocksize] [-R port:port] [host [port]] [-c command]
#XXX TFTP="/usr/bin/tftp -v -4 127.0.0.1 1234 -m binary"

TFTP="./tftpy_client.py --host=127.0.0.1 --port=1234 --tsize --quiet"
TFTPDIR=/tmp/tftpboot

cd ${PWD}

UNAME=`uname`
script_dir=`dirname "$0"`
ln -sf ${script_dir}/tftpy_client.py .

##############################################
if test "${UNAME}" == "Linux"; then
    sudo umount -t tmpfs ${TFTPDIR} || echo "OK"
    sudo rm -rf ${TFTPDIR}
else
    rm -rf ${TFTPDIR}
fi
##############################################

##############################################
## TODO: upload should fail, no tftpboot dir
# bin/tftpd_test 1234 &
# sleep 1
# ${TFTP} -m binary -c put build.ninja zero.dat && exit 1
# wait
##############################################

mkdir ${TFTPDIR}
chmod 700 ${TFTPDIR}
touch ${TFTPDIR}/zero.dat
chmod 400 ${TFTPDIR}/zero.dat

# NOTE: we start server at bg
# download must fail, dir not world readable
bin/tftpd_test 1234 &
sleep 1
chmod 700 ${TFTPDIR}
sync
${TFTP} --download=zero.dat && exit 1
wait

## TODO: upload should fail in secure mode if file not world writeable
# bin/tftpd_test 1234 &
# ${TFTP} -m binary -c put build.ninja zero.dat && exit 1
# wait

# must fail no such file
chmod 777 ${TFTPDIR}
bin/tftpd_test 1234 &
sleep 1
${TFTP} --download=none.dat && exit 1
wait


##############################################
if test "${UNAME}" == "Linux"; then
    sudo mount -t tmpfs -o size=224K,mode=0777 tmpfs ${TFTPDIR}
    df -h ${TFTPDIR}
fi
##############################################


##############################################
## test exact blocksize upload
dd if=bin/tftpd_test of=test1block.dat bs=512 count=1
bin/tftpd_test 1234 &
sleep 1
${TFTP} --upload=test1block.dat
diff ${TFTPDIR}/test1block.dat test1block.dat
wait
##############################################
## test modulo blocksize upload
dd if=bin/tftpd_test of=test1k.dat bs=1024 count=1
bin/tftpd_test 1234 &
sleep 1
${TFTP} --upload=test1k.dat
diff ${TFTPDIR}/test1k.dat test1k.dat
wait
##############################################
# NOTE: we start server at bg
# only one upload should fail
dd if=bin/tftpd_test of=test16k.dat bs=1024 count=16
bin/tftpd_test 1234 &
sleep 1
${TFTP} --input=test16k.dat --upload=first.dat &
${TFTP} --input=test16k.dat --upload=second.dat &
echo "concurend clients started! ..."
wait
test -f ${TFTPDIR}/first.dat && diff test16k.dat ${TFTPDIR}/first.dat
test -f ${TFTPDIR}/second.dat && diff test16k.dat ${TFTPDIR}/second.dat
##############################################
# download must fail
# not 1 client only!
touch ${TFTPDIR}/zero.dat
chmod 666 ${TFTPDIR}/zero.dat
bin/tftpd_test 1234 &
sleep 1
${TFTP} --download=zero.dat && exit 1
wait

##############################################
if test "${UNAME}" == "Linux"; then
    dd if=bin/tftpd_test of=test64k.dat bs=1024 count=64

    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --blksize=16384 --upload=test64k.dat --input=test64k.dat
    wait

    #---------------------------------------------
    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --blksize=32768 --upload=test64k.dat --input=test64k.dat
    wait

    #---------------------------------------------
    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --blksize=65536 --upload=test64k.dat --input=test64k.dat
    wait
else
    /bin/dd if=/dev/zero of=test64m.dat bs=1m count=64

    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --blksize=1024 --upload=test64m.dat --input=test64m.dat
    wait

    #---------------------------------------------
    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --blksize=2048 --upload=test64m.dat --input=test64m.dat
    wait

    #---------------------------------------------
    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --blksize=4096 --upload=test64m.dat --input=test64m.dat
    wait
fi
##############################################

##############################################
# NOTE we start our an own client
##############################################
# normal binary upload with dublicate ack's
# TODO: should not fail
## bin/tftpd_test 1234 &
## sleep 1
## printf "rexmt 1\nverbose\ntrace\nbinary\nput build.ninja\n" | bin/tftp 127.0.0.1 1234
## test -f ${TFTPDIR}/build.ninja && diff ${TFTPDIR}/build.ninja build.ninja
## wait
##############################################

##############################################
# upload large file > 135K:
# NOTE: must fail, disk full!
if test "${UNAME}" == "Linux"; then
    bin/tftpd_test 1234 &
    sleep 1
    ${TFTP} --input=bin/tftpd_test --upload=tftpd_test && exit 1
    #XXX diff ${TFTPDIR}/tftpd_test bin/tftpd_test || echo "OK"
    wait
fi
##############################################
## absolut path upload must fail
dd if=bin/tftpd_test of=test32k.dat bs=1024 count=32
bin/tftpd_test 1234 &
sleep 1
${TFTP} --input=test32k.dat --upload=${TFTPDIR}/test32k.dat && exit 1
wait
##############################################

# TODO: ascii upload should fail
## bin/tftpd_test 1234 &
## sleep 1
## ${TFTP} -m ascii -c put build.ninja && exit 1
## wait

# relative path upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} --input=build.ninja --upload=../build.ninja && exit 1
wait

# relative path upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} --input=build.ninja --upload=./../build.ninja && exit 1
wait

# invalid absolut path upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} --input=build.ninja --upload=//srv///tftp/build.ninja && exit 1
wait

# relative path to nonexisting subdir must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} --input=build.ninja --upload=./tftp/build.ninja && exit 1
wait

##############################################
echo "test idle timeout (10 seconds) ..."
# port must be free, no error expected
bin/tftpd_test 1234

# test help
bin/tftpd_test || echo "OK"

