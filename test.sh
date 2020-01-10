#!/bin/bash -uex
TFTP="/usr/bin/tftp -v -4 127.0.0.1 1234"
TFTPDIR=/tmp/tftpboot

cd ${PWD}

sudo umount -t tmpfs ${TFTPDIR} || echo "OK"
rm -rf ${TFTPDIR}

##############################################
# upload should fail, no tftpboot dir
# bin/tftpd_test 1234 &
# sleep 1
# ${TFTP} -m binary -c put rules.ninja zero.dat && exit 1
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
${TFTP} -m binary -c get zero.dat zero.dat && exit 1
wait

## upload should fail in secure mode if file not world writeable
# bin/tftpd_test 1234 &
# ${TFTP} -m binary -c put rules.ninja zero.dat && exit 1
# wait

# must fail no such file
chmod 777 ${TFTPDIR}
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c get none.dat && exit 1
wait


##############################################
sudo mount -t tmpfs -o size=160K,mode=0777 tmpfs ${TFTPDIR}
df -h ${TFTPDIR}
##############################################

##############################################
## test exact blocksize upload
dd if=bin/tftpd_test of=test1block.dat bs=512 count=1
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put test1block.dat
diff ${TFTPDIR}/test1block.dat test1block.dat
wait
##############################################
## test modulo blocksize upload
dd if=bin/tftpd_test of=test1k.dat bs=1024 count=1
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put test1k.dat
diff ${TFTPDIR}/test1k.dat test1k.dat
wait
##############################################
# NOTE: we start server at bg
# only one upload should fail
dd if=bin/tftpd_test of=test16k.dat bs=1024 count=16
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put test16k.dat first.dat &
${TFTP} -m binary -c put test16k.dat second.dat &
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
${TFTP} -m binary -c get zero.dat zero.dat && exit 1
wait

##############################################
# NOTE we start our an own client
##############################################
# normal binary upload with dublicate ack's
# TODO: should not fail
bin/tftpd_test 1234 &
sleep 1
printf "rexmt 1\nverbose\ntrace\nbinary\nput rules.ninja\n" | bin/tftp 127.0.0.1 1234
test -f ${TFTPDIR}/rules.ninja && diff ${TFTPDIR}/rules.ninja rules.ninja
wait
##############################################
## upload large file > 135K:
#XXX must fail, disk full!
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put bin/tftpd_test tftpd_test && exit 1
#XXX diff ${TFTPDIR}/tftpd_test bin/tftpd_test
wait
##############################################
## absolut path upload must fail
dd if=bin/tftpd_test of=test32k.dat bs=1024 count=32
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put test32k.dat ${TFTPDIR}/test32k.dat && exit 1
wait
##############################################

# ascii upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m ascii -c put rules.ninja && exit 1
wait

# relative path upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put rules.ninja ../rules.ninja && exit 1
wait

# relative path upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put rules.ninja ./../rules.ninja && exit 1
wait

# invalid absolut path upload must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put rules.ninja //srv///tftp/rules.ninja && exit 1
wait

# relative path to nonexisting subdir must fail
bin/tftpd_test 1234 &
sleep 1
${TFTP} -m binary -c put rules.ninja ./tftp/rules.ninja && exit 1
wait

##############################################
echo "test idle timeout (10 seconds) ..."
# port must be free, no error expected
bin/tftpd_test 1234

# test help
bin/tftpd_test || echo "OK"

