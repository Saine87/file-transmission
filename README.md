file-transmission
==================

File transmission via UDP protocol

<p>This is a simple project built in C that allows the transmission of files between a client and a server. In particular the client can download or upload a file from the server. The use of UDP protocol makes the solution interesting from the academic point of view because we know that the UDP protocol has no handshaking dialogues, and thus exposes any unreliability of the underlying network protocol to the user's program. The reliability of the transmission is entirely managed by the messages exchanged between the client and the server.</p>
<p>In the report, written in Italian are explained in detail the architecture used and the messages exchanged between the two end points for the transmission of the file.</p>
