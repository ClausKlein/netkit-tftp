#!/bin/sh -uex
TFTP="/usr/bin/tftp -v -4 127.0.0.1 1234"
TFTPDIR=/tmp/tftpboot

cd ${PWD}

rm -rf ${TFTPDIR}
mkdir ${TFTPDIR}
chmod 755 ${TFTPDIR}
touch ${TFTPDIR}/zero.data
chmod 400 ${TFTPDIR}/zero.data


# NOTE: we start server at bg
# download must fail, dir not world readable
bin/tftpd_test 1234 &
${TFTP} -m binary -c get rules.ninja zero.data || date && sleep 1

# upload should fail, file not world writeable
bin/tftpd_test 1234 &
${TFTP} -m binary -c put rules.ninja zero.data || date && sleep 1

# must fail no such file
chmod 777 ${TFTPDIR}
bin/tftpd_test 1234 &
${TFTP} -m binary -c get zero.dat || date && sleep 1


##############################################
# normal binary upload with dublicate ack's
bin/tftpd_test 1234 &
printf "verbose\ntrace\nbinary\nput rules.ninja\n" | bin/tftp 127.0.0.1 1234
diff ${TFTPDIR}/rules.ninja rules.ninja
sleep 3
##############################################
## test modulo blocksize upload
# dd if=bin/tftpd_test of=test1k.dat bs=1024 count=1
# bin/tftpd_test 1234 &
# ${TFTP} -m binary -c put test1k.dat
# diff ${TFTPDIR}/test1k.dat test1k.dat
# sleep 3
##############################################
## test exact blocksize upload
# dd if=bin/tftpd_test of=test1block.dat bs=512 count=1
# bin/tftpd_test 1234 &
# printf "verbose\ntrace\nbinary\nput test1block.dat\n" | bin/tftp 127.0.0.1 1234
# diff ${TFTPDIR}/test1block.dat test1block.dat
# sleep 3
##############################################
## upload large file
bin/tftpd_test 1234 &
${TFTP} -m binary -c put bin/tftpd_test tftpd_test
diff ${TFTPDIR}/tftpd_test bin/tftpd_test
sleep 3
##############################################
## absolut path upload
dd if=bin/tftpd_test of=test32k.dat bs=1024 count=32
bin/tftpd_test 1234 &
${TFTP} -m binary -c put test32k.dat ${TFTPDIR}/test32k.dat
diff ${TFTPDIR}/test32k.dat test32k.dat
sleep 3
##############################################


# NOTE: we start server at bg
# download must fail
bin/tftpd_test 1234 &
touch ${TFTPDIR}/test16k.dat
chmod 666 ${TFTPDIR}/test16k.dat
${TFTP} -m binary -c get test16k.dat && date && sleep 1

# ascii upload must fail
${TFTP} -m ascii -c put rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# relative path upload must fail
${TFTP} -m binary -c put rules.ninja ../rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# relative path upload must fail
${TFTP} -m binary -c put rules.ninja ./../rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# invalid absolut path upload must fail
${TFTP} -m binary -c put rules.ninja ${PWD}/rules.ninja &
bin/tftpd_test 1234 || date && sleep 1

# relative path to nonexisting subdir must fail
${TFTP} -m binary -c put rules.ninja ./srv/tftp/rules.ninja &
bin/tftpd_test 1234 || date && sleep 1


##############################################
# test timeout
# port must be free
bin/tftpd_test 1234 ### || echo "OK"

# test help
bin/tftpd_test || echo "OK"

