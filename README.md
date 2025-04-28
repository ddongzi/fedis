

Main page

√ Branch:withoutConnection0421

主页！！
REDIS

 ./bin/redis --sentinel --config conf/server.conf --port 7000


Client

./client/a.out --port 6666
 slaveof 127.0.0.1:6668

