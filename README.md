# CauchyCaterpillar
CCat : The Cauchy Caterpillar - Streaming Erasure Code With Short Memory

ccat.h provides a simple C API for streaming erasure codes with a
fixed window.  This is designed for applications that do not need
data reliability, but want to improve robustness to packetloss by
using UDP sockets.  It supports up to 30% redundancy.

Example applications:
* VoIP
* Video conferencing/live broadcast
* Live telemetry/statistics feeds

Usage:

(1) Call ccat_create() to create a CCatCodec object.

(2) When sending a packet, pass it to ccat_encode_original().

(3) To encode recovery data, call ccat_encode_recovery(), which generates
a packet that can be sent over the network to fill in for losses.

(4) When receiving a packet, pass originals to ccat_decode_original().
Pass encoded data to the ccat_decode_recovery() function.  When recovery
occurs it will call the application's OnRecoveredData() callback.

Thread-safety:

Applications using the library can use different locks to protect the
ccat_encode_*() functions and the ccat_decode_*() functions, because no data
is shared between those.  Otherwise the library is not thread-safe and
does require locking on the application-side.

Packet de-duplication:

CCat will not deliver two packets with the same sequence number.

Packet re-ordering:

CCat can deliver data out of order, so its output should be fed into
a dejitter buffer.

Alternatives:

Compared to Random Linear Codes, the decoding is 2x more reliable.
Encoding/decoding is faster thanks to the Cauchy matrix structure.
The downside is that CCat is restricted to 2000 packets per second,
meaning that it is not suitable for streams faster than 2 MB/s.

For faster streams, using Siamese FEC is recommended:
https://github.com/catid/siamese/

For fountain codes, using Wirehair FEC is recommended:
https://github.com/catid/wirehair/


#### Credits

I posted about this type of erasure code years ago on my blog, but never finished the project.  Recently Nicolas SAID sent me a recent paper from Martin Reisslein from ASU that explores a similar idea: http://mre.faculty.asu.edu/CRLNC.pdf  Based on the success of that work I decided to put my own spin on it and release here.  Hope others find it useful!

Software by Christopher A. Taylor <mrcatid@gmail.com>

Please reach out if you need support or would like to collaborate on a project.