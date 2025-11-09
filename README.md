# DropBox
A multi-threaded file storage server inspired by Dropbox.


It is developed as part of Operating Systems Lab to demonstrate practical understanding of thread synchronization, producer–consumer architecture, and network programming in C.

<br> 👥 Developers

Huzaifa Saleem   - BSCS 23139<br>
Danish Javed Dar - BSCS 23187<br>
Abdullah Asim    - BSCS 23064<br>


<br>🛠️ Running: <br>


1=Clone the repository:<br>  
      git clone <repository-url><br> 
      cd dropbox_server<br>

<br>2=Build the server and test client:  <br>
      make clean <br> 
      make   

<br>3=Start the Server:  
     <br> ./server 8080  <br>
    

<br>4= Run automated test on another terminal  <br>
      ./test_client 127.0.0.1 8080 test1  <br>   
      

<br>5=For interactive Testing  with live commands:  <br>
      ./test_client 127.0.0.1 8080 interactive  <br>

      
<br>

## Testing

<br> Valgrind Test Command:<br>
<img width="404" height="33" alt="val 1" src="https://github.com/user-attachments/assets/fcb4ebbd-93de-4751-a3af-a38e0bcdcbbc" />

<br>Valgrind Result :<br>
<img width="397" height="124" alt="val 2" src="https://github.com/user-attachments/assets/19898c3c-79e5-405d-ab8d-67f30be355ec" />






