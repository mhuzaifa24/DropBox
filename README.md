# DropBox
A multi-threaded file storage server inspired by Dropbox.


It is developed as part of Operating Systems Lab to demonstrate practical understanding of thread synchronization, producer–consumer architecture, and network programming in C.

👥 Developers

Huzaifa Saleem   - BSCS 23139   //
Danish Javed Dar - BSCS 23187   // 
Abdullah Asim    - BSCS 23064   // 


🛠️ Running:


1=Clone the repository:
    git clone <repository-url>
    cd dropbox_server
    
2=Build the server and test client:
    make clean
    make

3=Start the Server:
    ./server 8080

4= Run automated test on another terminal
    ./test_client 127.0.0.1 8080 test1   

5=For interactive Testing  with live commands:
    ./test_client 127.0.0.1 8080 interactive



