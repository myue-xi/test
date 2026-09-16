# Windows编译配置
CC = g++
CFLAGS = -std=c++11 -O2 -g -Wall -Wextra
INCLUDES = -I./include
LDFLAGS = -lpthread -lws2_32

# Linux编译配置
# CC = g++
# CFLAGS = -std=c++11 -O2 -g -Wall -Wextra
# INCLUDES = -I./include
# LDFLAGS = -lpthread

# 源文件
SRC_SERVER = src/echo_server.cpp
SRC_CLIENT = src/echo_client.cpp
SRC_SERVER_WIN = src/echo_server_windows.cpp
SRC_CLIENT_WIN = src/echo_client_windows.cpp

# 目标文件
SERVER = server
CLIENT = client
SERVER_WIN = server.exe
CLIENT_WIN = client.exe

# 默认目标（Windows）
all: $(SERVER_WIN) $(CLIENT_WIN)

# Linux目标
all_linux: $(SERVER) $(CLIENT)

# 编译Windows服务器
$(SERVER_WIN): $(SRC_SERVER_WIN)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS)

# 编译Windows客户端
$(CLIENT_WIN): $(SRC_CLIENT_WIN)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS)

# 编译Linux服务器
$(SERVER): $(SRC_SERVER)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS)

# 编译Linux客户端
$(CLIENT): $(SRC_CLIENT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS)

# 清理
clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_WIN) $(CLIENT_WIN)

.PHONY: all all_linux clean
