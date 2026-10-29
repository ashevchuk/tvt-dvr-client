# Vendor SDK (optional)

The client works without any proprietary software. The manufacturer's network SDK is only needed
for the `sdk` backend (`make SDK=1`), which is kept to compare against the native implementation.
It is proprietary and is not redistributed with this project. Obtain the Linux x86-64 SDK for your
device from the manufacturer and put it here as `vendor/sdk` (a symlink works):

```
vendor/sdk/include/DVR_NET_SDK.h        (and the headers it includes)
vendor/sdk/Linux/bin/libdvrnetsdk.so
vendor/sdk/Linux/bin/libasound.so       (the copy shipped with the SDK)
```

Everything in this directory except this file is ignored by git. To build against an SDK kept
elsewhere, pass `make SDK=1 SDK_DIR=/path/to/sdk`.

The build was developed against SDK `1.2.1.019` (2019-09-02). The public headers are in GBK
encoding; use `iconv -f gbk -t utf-8` to read their comments.
