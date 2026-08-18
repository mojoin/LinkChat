// protocol.h: 协议层
//
// 职责：把"一整行 JSON 字符串"翻译成"另一整行 JSON 字符串"。
//       只做字符串/JSON 解析与构造，【不】碰 socket / fd / send / recv。
//
// 设计原则：纯函数，方便单元测试（不起服务器就能测）。
//
// 调用方约定：
//   1. 调用方负责按 \n 切行，传入的 line 【不】含 \n。
//   2. 返回的 reply 【不】含 \n，由调用方 send 完 reply 后再 send "\n"。
#pragma once

#include <string>

// 收到完整一行 JSON（不含 \n）后调用。
// 返回要发给客户端的一行 JSON（不含 \n）。
// 任何错误都用统一格式：{"type":"error","msg":"...","ok":false}
std::string handleMessage(const std::string &line, int from_uid, int fd);
