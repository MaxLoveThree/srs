ulimit -c unlimited
./objs/srs -c conf/publish_edge.conf
./objs/srs -c conf/play_edge.conf
./objs/srs -c conf/origin.conf

