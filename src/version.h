/* These macros are used where the server name is printed in logs and replies.
 * Note the difference in the first letter "V" vs "v". SERVER_TITLE is used in
 * readable text like log messages and SERVER_NAME is used in INFO fields and
 * similar. */
<<<<<<< HEAD
#define SERVER_NAME "kv"
#define SERVER_TITLE "KV"
#define KV_VERSION "255.255.255"
#define KV_VERSION_NUM 0x00ffffff
=======
#define SERVER_NAME "valkey"
#define SERVER_TITLE "Valkey"
#define VALKEY_VERSION "9.0.4"
#define VALKEY_VERSION_NUM 0x00090004
>>>>>>> v9.0.4
/* The release stage is used in order to provide release status information.
 * In unstable branch the status is always "dev".
 * During release process the status will be set to rc1,rc2...rcN.
 * When the version is released the status will be "ga". */
<<<<<<< HEAD
#define KV_RELEASE_STAGE "dev"
=======
#define VALKEY_RELEASE_STAGE "ga"
>>>>>>> v9.0.4

/* Redis OSS compatibility version, should never
 * exceed 7.2.x. */
#define REDIS_VERSION "7.2.4"
#define REDIS_VERSION_NUM 0x00070204
