Programming Assignment - FTP Server and Client
Version 1.0 08/04/2019
CPSC-471, summer 2019

Contributors
----------------------------------------------------------------
Allen Mrazek			amrazek@csu.fullerton.edu
David Williams-Haven		dgwh1995@csu.fullerton.edu
Daniel Walsh			Danielwalsh27@csu.fullerton.edu
Mitchell Norseth		mitchell2124@csu.fullerton.edu
----------------------------------------------------------------

Usage notes
----------------------------------------------------------------
- Programming language: C++

- Supports both Windows and Linux operating systems
----------------------------------------------------------------

How to execute
----------------------------------------------------------------
Windows:
- open 2 separate command prompt windows on the same machine
  or a command prompt on one machine and another on a different 
  machine
- navigate to the folder containing Server.exe and Client.exe
- type Server.exe <server port> on one of the cmd windows to
  start up the server
- type Client.exe <server machine> <server port> on the other 
  cmd window
- the FTP client will connect to the FTP server
- client may now interact with the server by executing commands   
  such as 'exit', 'ls', 'get', 'put', and 'q' 

Linux:
- open 2 separate terminals on the same machine or a terminal on  
  one machine and another on a different machine 
- navigate to the linux folder in the project file
- type 'make' to compile the project
- navigate to linux/bin folder to locate the executables
- execute command './Server <server port>' on one terminal to 
  start up the server 
- execute command './Client.exe <server machine> <server port>' 
  on the other terminal
- the FTP client will connect to the FTP server 
- client may now interact with the server by executing commands   
  such as 'exit', 'ls', 'get', 'put', and 'q' 
----------------------------------------------------------------

 