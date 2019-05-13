# ChatServer

2018년 전공 3학년 1학기 게임서버 과목에서 개발했던 채팅서버 입니다  
서버가 있고, 클라이언트들이 서버에 접속하여 채팅을 할 수 있습니다
<br /><br />
서버는 먼저 쓰레드를 여러 개 만들고, Gate를 생성한 뒤 listen을 시작합니다  
해당 쓰레드는 비동기식으로 클라이언트의 연결요청을 기다리며,  
연결요청이 오면 세션(소켓)을 동적으로 할당하고, 한 쓰레드에게 세션을 담당하게 합니다
<br /><br />
각각의 세션은 클라이언트와의 통신을 1:1로 담당하게 되며,  
이때 쓰레드는 동기식으로 클라이언트의 패킷(메세지)을 기다리게 됩니다  
하지만 서버에서 클라이언트로 보내는 패킷은 비동기식으로 처리됩니다 
<br /><br />
클라이언트도 마찬가지로 먼저 쓰레드를 생성한 후 소켓을 생성합니다  
그리고 소켓을 통해 서버에 연결요청을 합니다  
연결이 되면 패킷을 보내는 Send와 받는 Receive 모두 비동기식으로 처리됩니다
<br /><br />
채팅서버가 제공하는 기능으로는 아이디 설정, 방 생성 및 이동, 귓속말 등이 있습니다  
클라이언트는 :set , :createRoom, :to 등의 특수한 명령어로 이를 서버에 요청할 수 있습니다  
서버는 특수 명령어의 성공/실패 여부를 클라이언트에게 피드백 해줍니다
<br /><br /><br />
**데모영상**은 여기서 보실 수 있습니다  
https://youtu.be/4WZDjZ1ZhlU
