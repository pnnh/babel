#include "babel/src/ipc/IpcMessage.h"

#include <gtest/gtest.h>

using babel::ipc::IpcMessage;
using babel::ipc::MessageType;
using babel::ipc::tryExtractFrame;

TEST(IpcProtocolTest, SerializeAndDeserializeRoundTrip) {
  IpcMessage msg;
  msg.seq = 42;
  msg.type = MessageType::Hello;
  msg.rawPayloadJson = R"({"client":"swift_imk"})";

  const std::vector<uint8_t> frame = msg.serialize();
  ASSERT_GE(frame.size(), 4u);

  std::vector<uint8_t> buffer = frame;
  const auto extracted = tryExtractFrame(buffer);
  ASSERT_TRUE(extracted.has_value());
  EXPECT_EQ(extracted->seq, 42u);
  EXPECT_EQ(extracted->type, MessageType::Hello);
  EXPECT_TRUE(buffer.empty());
}

TEST(IpcProtocolTest, PartialBufferReturnsNulloptUntilComplete) {
  IpcMessage msg;
  msg.seq = 100;
  msg.type = MessageType::KeyDown;
  msg.rawPayloadJson = R"({"keyCode":49})";

  const std::vector<uint8_t> frame = msg.serialize();
  ASSERT_GT(frame.size(), 6u);

  // 只喂入前 5 个字节（部分数据）
  std::vector<uint8_t> partialBuffer(frame.begin(), frame.begin() + 5);
  EXPECT_FALSE(tryExtractFrame(partialBuffer).has_value());
  EXPECT_EQ(partialBuffer.size(), 5u);

  // 补齐剩余字节
  partialBuffer.insert(partialBuffer.end(), frame.begin() + 5, frame.end());
  const auto extracted = tryExtractFrame(partialBuffer);
  ASSERT_TRUE(extracted.has_value());
  EXPECT_EQ(extracted->seq, 100u);
  EXPECT_EQ(extracted->type, MessageType::KeyDown);
  EXPECT_TRUE(partialBuffer.empty());
}

TEST(IpcProtocolTest, MakeKeyDownResponseGeneratesValidJson) {
  babel::ipc::KeyDownResponsePayload payload;
  payload.consumed = true;
  payload.action = "UPDATE_PREEDIT";
  payload.markedText = "nihao";
  payload.commitText = "";

  const IpcMessage resp = IpcMessage::makeKeyDownResponse(123, payload);
  EXPECT_EQ(resp.seq, 123u);
  EXPECT_EQ(resp.type, MessageType::KeyDown);

  const auto frame = resp.serialize();
  std::vector<uint8_t> buffer = frame;
  const auto extracted = tryExtractFrame(buffer);
  ASSERT_TRUE(extracted.has_value());
  EXPECT_NE(extracted->rawPayloadJson.find("UPDATE_PREEDIT"), std::string::npos);
  EXPECT_NE(extracted->rawPayloadJson.find("nihao"), std::string::npos);
}

