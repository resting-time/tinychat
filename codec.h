#pragma once
#include <cstdint>
#include <vector>
#include<string>
#include<unistd.h>
#include "msg.pb.h"

inline bool send_msg(int fd, const tc::Wrapper &w) {
    std::string bin;
    if (!w.SerializeToString(&bin)) return false;
    uint32_t len = bin.size();
    std::vector<char> buf(4 + len);
    memcpy(buf.data(), &len, 4);
    memcpy(buf.data() + 4, bin.data(), len);
    return write(fd, buf.data(), 4 + len) == (ssize_t)(4 + len);
}

inline bool recv_msg(int fd, tc::Wrapper &w) {
    uint32_t len;
    if (read(fd, &len, 4) != 4) return false;
    std::string bin(len, '\0');
    if (read(fd, &bin[0], len) != (ssize_t)len) return false;
    return w.ParseFromString(bin);
}
