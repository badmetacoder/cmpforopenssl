myDir=`dirname $0`

BINDIR="${myDir}/../bin"
CERTDIR="${myDir}/../certs"

CMPCLIENT="${BINDIR}/cmpclient"
CMPSERVER="${BINDIR}/cmpserver-cl"

# for INSTA demo CA
INSTA_SERVER="pki.certificate.fi"
INSTA_PORT="8700"
INSTA_SERVERPATH="pkix/"
INSTA_CACERT="${CERTDIR}/insta_ca_cert.der"
INSTA_USER="3078"
INSTA_PASS="insta"

# for private demo CA
SERVER="172.16.0.101"
PORT="4711"
CACERT="${CERTDIR}/ca_cert.der"

CAKEY="${CERTDIR}/ca_key.p15"
CLCERT="${CERTDIR}/cl_cert.der"
CLKEY="${CERTDIR}/cl_key.pem"
NEWCLCERT="${CERTDIR}/cl_kup_cert.der"
NEWCLKEY="${CERTDIR}/cl_kup_key.pem"

KEYLENGTH=2048
OPENSSL="openssl"

COUNTRY="DE"
ORG="NSN"
UNIT="PG RDE 3"
CN="Martin Peylo"

SRVCOUNTRY="DE"
SRVORG="NSN"
SRVUNIT="PG RDE 3"
SRVCN="Martin's CA"
