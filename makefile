# 编译器
CC = gcc
CFLAGS = -Wall -g -Iinclude	
LDFLAGS = -lssl -lcrypto

# 目录
SRC_DIR = src
TEST_DIR = test
BIN_DIR = bin


# 目标程序
TARGETS = $(BIN_DIR)/redis
TESTS = $(BIN_DIR)/test_dict $(BIN_DIR)/test_db $(BIN_DIR)/test_sds $(BIN_DIR)/test_list

# 源文件（手动指定）
REDIS_SRC = $(SRC_DIR)/ae.c $(SRC_DIR)/client.c $(SRC_DIR)/conf.c $(SRC_DIR)/crypto.c \
	$(SRC_DIR)/db.c $(SRC_DIR)/dict.c \
	$(SRC_DIR)/list.c $(SRC_DIR)/log.c $(SRC_DIR)/main.c \
	$(SRC_DIR)/net.c $(SRC_DIR)/rdb.c $(SRC_DIR)/redis.c \
	$(SRC_DIR)/repli.c $(SRC_DIR)/sds.c \
	 $(SRC_DIR)/robj.c $(SRC_DIR)/rio.c  $(SRC_DIR)/util.c 


TEST_DICT_SRC = $(SRC_DIR)/dict.c $(TEST_DIR)/test_dict.c 
TEST_DB_SRC = $(TEST_DIR)/test_db.c $(SRC_DIR)/db.c $(SRC_DIR)/dict.c \
	$(SRC_DIR)/list.c  $(SRC_DIR)/sds.c $(SRC_DIR)/robj.c 
TEST_LIST_SRC = $(TEST_DIR)/test_list.c $(SRC_DIR)/list.c
TEST_SDS_SRC = $(TEST_DIR)/test_sds.c $(SRC_DIR)/sds.c
TEST_RIO_SRC = $(TEST_DIR)/test_rio.c $(SRC_DIR)/rio.c $(SRC_DIR)/sds.c


# 默认目标：编译所有
all: $(TARGETS) $(TESTS)


# 编译 `redis`
$(BIN_DIR)/redis: $(REDIS_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) 

# 编译 `test_dict`
$(BIN_DIR)/test_dict: $(TEST_DICT_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

# 编译 `test_db`
$(BIN_DIR)/test_db: $(TEST_DB_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

# 编译 `test_list`
$(BIN_DIR)/test_list: $(TEST_LIST_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

# 编译 `test_sds`
$(BIN_DIR)/test_sds: $(TEST_SDS_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

# 编译 `test_sds`
$(BIN_DIR)/test_rio: $(TEST_RIO_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@


# 运行所有测试
test: $(TESTS)
	@for t in $(TESTS); do \
		echo "Running $$t..."; \
		$$t || exit 1; \
	done
	@echo "\n🎉 All tests passed!"

# 清理
clean:
	rm -rf $(BIN_DIR)
