ZCF
---

ZeoMQ Communication Framework

ZeroMQ based framework implementing:

* Sycnhronous RPC through REQ-REP
* Asynchronous streaming - subscriptions through PUB-SUB


ZCF RPC
-------

Client (Proxy) and Server (Implementation) objects.

Create single include file with enums identifying procedures.

Invocation from Client:

```c++

 #include "procedure-ids.h"

 ZCF::RPC rpc("tcp://localhost:5556");

 vector<string> files = rpc[LIST_FILES]("/path/");

```

Setup on Server:

```c++

 #include "procedure-ids.h"
 
 ZCF::RPCServer rpcserver("tcp://*:5556");
 
 function< vector<string> (const string&) > listFiles 
    = [](const string& path) {
       ... 
       d = opendir(path.c_str());
       ... 
       for(...) {
         result.push_back(
       }
       return result;
    };
 rpcserver[LIST_FILES] = listFiles; 

```

Transport: 

```
|ID<int32_t>|LENGTH<int32_t>|bytes<int8_t>*|

ID used for status as well; reserve first three positive integers
STATUS: {OK = 0, ERROR = 1, INFO = 2,...}

Limitation on 2GB is on purpose.
```

ZCF Streaming
-------------

Stream through PUB socket.

Receive with SUB socket.

Same ZCF::RPCServer class can be used for both sync and pub-sub patterns.

Implement a RAWStreamer I/O class as a thin wrapper on top of PUB socket +
async queue + De/Se-rializer template parameter. Empty message terminates
the communication.


Serialization
-------------

Procedure parameters and return values need to be de/se-rialized from/to
byte arrays.

Options:

* provide compile type specialization for types through Pack<T>/Unpack<T> classess/functions
* register serializers for each procedure and a default one in case none is found; procedure can share serialzers

Compression/encryption can be handled directly through the serializers.





























