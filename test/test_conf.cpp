#include <gtest/gtest.h>


extern "C" {
#include "util.h"
#include "conf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
    }

TEST(ConfTest, conf)
{
    char* filename = fullPath("conf/server-6666.conf");
    ASSERT_NE(filename, nullptr);
    char* role = get_config(filename, "role");
    ASSERT_NE(role, nullptr);
    update_config(filename, "role", "slave");
    role = get_config(filename, "role");
    ASSERT_NE(role, nullptr);
    EXPECT_STREQ(role, "slave");
    free(role);
    free(filename);
}