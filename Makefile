# UTILITY_DIR = utility/
# UTILITY_FILES = $(UTILITY_DIR)/AsyncQueue/AsyncQueue.h $(UTILITY_DIR)/AsyncQueue/AsyncQueue.c \
# 	$(UTILITY_DIR)/FileSystem/FileSystem.h $(UTILITY_DIR)/FileSystem/FileSystem.c \
# 	$(UTILITY_DIR)/HashTable/HashTable.h $(UTILITY_DIR)/HashTable/HashTable.c \
# 	$(UTILITY_DIR)/LinkedList/LinkedList.h $(UTILITY_DIR)/LinkedList/LinkedList.c \
# 	$(UTILITY_DIR)/Queue/Queue.h $(UTILITY_DIR)/Queue/Queue.c \
# 	$(UTILITY_DIR)/SDSet/SDSet.h $(UTILITY_DIR)/SDSet/SDSet.c 
# UTILITY_OBJS = $(UTILITY_FILES:.c=.0)

HEADER_FILES = common/constant.h 

OBJ_FILES = utility/HashTable/HashTable.o utility/LinkedList/LinkedList.o utility/AsyncQueue/asyncqueue.o \
	utility/Queue/queue.o utility/ChunkyFile/ChunkyFile.o utility/FileSystem/FileSystem.o utility/FileTable/FileTable.o utility/SDSet/SDSet.o \
	common/peer_table.o utility/ColoredPrint/ColoredPrint.o common/packets.o

CFLAGS = -Wall -pedantic -std=gnu99 -g -pthread -lm

all: tracker/tracker_app client/client_app

test: tracker/test_tracker_network client/test_client_network

client: client/client_app

tracker: tracker/tracker_app

tracker/tracker_app: tracker/tracker.c tracker/tracker.h tracker/network_tracker.o $(OBJ_FILES) $(HEADER_FILES)
	gcc $(CFLAGS) tracker/tracker.c tracker/network_tracker.o $(OBJ_FILES) -o tracker/tracker_app

tracker/test_tracker_network: tracker/test_tracker_network.c tracker/network_tracker.o $(OBJ_FILES) $(HEADER_FILES)
	gcc $(CFLAGS) tracker/test_tracker_network.c tracker/network_tracker.o $(OBJ_FILES) -o tracker/test_tracker_network

client/client_app: client/client.c client/client.h client/network_client.o $(OBJ_FILES) $(HEADER_FILES)
	gcc $(CFLAGS) client/client.c client/network_client.o $(OBJ_FILES) -o client/client_app

client/test_client_network: client/test_client_network.c client/network_client.o $(OBJ_FILES) $(HEADER_FILES)
	gcc $(CFLAGS) client/test_client_network.c client/network_client.o $(OBJ_FILES) -o client/test_client_network

tracker/network_tracker.o: tracker/network_tracker.c tracker/network_tracker.h $(OBJ_FILES) $(HEADER_FILES)
	gcc $(CFLAGS) -c tracker/network_tracker.c -o tracker/network_tracker.o

client/network_client.o: client/network_client.c client/network_client.h $(OBJ_FILES) $(HEADER_FILES)
	gcc $(CFLAGS) -c client/network_client.c -o client/network_client.o

common/packets.o: common/packets.h common/packets.c 
	gcc $(CFLAGS) -c common/packets.c -o common/packets.o

common/peer_table.o: common/peer_table.h common/peer_table.c $(HEADER_FILES)
	gcc $(CFLAGS) -c common/peer_table.c -o common/peer_table.o

utility/ColoredPrint/ColoredPrint.o: utility/ColoredPrint/ColoredPrint.c utility/ColoredPrint/ColoredPrint.h utility/Queue/queue.o
	gcc $(CFLAGS) -c utility/ColoredPrint/ColoredPrint.c -o utility/ColoredPrint/ColoredPrint.o

utility/ChunkyFile/ChunkyFile.o: utility/ChunkyFile/ChunkyFile.c utility/ChunkyFile/ChunkyFile.h
	gcc $(CFLAGS) -c utility/ChunkyFile/ChunkyFile.c -o utility/ChunkyFile/ChunkyFile.o

utility/FileSystem/FileSystem.o: utility/FileSystem/FileSystem.c utility/FileSystem/FileSystem.h
	gcc $(CFLAGS) -c utility/FileSystem/FileSystem.c -lm -o utility/FileSystem/FileSystem.o

utility/FileTable/FileTable.o: utility/FileTable/FileTable.c utility/FileTable/FileTable.h
	gcc $(CFLAGS) -c utility/FileTable/FileTable.c -o utility/FileTable/FileTable.o

utility/HashTable/HashTable.o: utility/HashTable/HashTable.c utility/HashTable/HashTable.h utility/LinkedList/LinkedList.o
	gcc $(CFLAGS) -c utility/HashTable/HashTable.c -o utility/HashTable/HashTable.o 

utility/LinkedList/LinkedList.o: utility/LinkedList/LinkedList.c utility/LinkedList/LinkedList.h
	gcc $(CFLAGS) -c utility/LinkedList/LinkedList.c -o utility/LinkedList/LinkedList.o 

utility/AsyncQueue/asyncqueue.o: utility/AsyncQueue/AsyncQueue.c utility/AsyncQueue/AsyncQueue.h utility/Queue/queue.o
	gcc -pthread -g -c utility/AsyncQueue/AsyncQueue.c -o utility/AsyncQueue/asyncqueue.o

utility/Queue/queue.o: utility/Queue/Queue.c utility/Queue/Queue.h
	gcc $(CFLAGS) -c utility/Queue/Queue.c -o utility/Queue/queue.o

utility/SDSet/SDSet.o: utility/SDSet/SDSet.c utility/SDSet/SDSet.h
	gcc $(CFLAGS) -c utility/SDSet/SDSet.c -o utility/SDSet/SDSet.o

clean:
	rm -rf $(OBJ_FILES)
	rm -rf tracker/network_tracker.o
	rm -rf client/network_client.o
	rm -rf client/client_app
	rm -rf tracker/tracker_app
	rm -rf tracker/*.dSYM
	rm -rf tracker/test_tracker_network
	rm -rf client/*.dSYM
	rm -rf client/test_client_network
	rm -rf ~/dart_sync/

