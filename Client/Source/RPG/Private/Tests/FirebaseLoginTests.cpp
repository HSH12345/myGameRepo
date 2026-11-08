#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "RPG/Packet/BinaryPackets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirebaseLoginCodecTest, "RPG.Auth.FirebaseCodec",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirebaseLoginCodecTest::RunTest(const FString&)
{
    using namespace RpgProtocol;
    FFirebaseLoginRequestPacket request;
    request.requestId = 0x01020304;
    request.idToken = TEXT("synthetic-token");
    TArray<uint8> bytes;
    TestTrue(TEXT("Encode token request"), request.Serialize(bytes));
    TestEqual(TEXT("Request BE first byte"), bytes[0], uint8(1));
    TestEqual(TEXT("Request BE fourth byte"), bytes[3], uint8(4));
    FFirebaseLoginRequestPacket parsed;
    TestTrue(TEXT("Decode token request"), parsed.Deserialize(bytes.GetData(), bytes.Num()));
    TestEqual(TEXT("Token roundtrip"), parsed.idToken, request.idToken);
    bytes.Add(0);
    TestFalse(TEXT("Reject trailing bytes"), parsed.Deserialize(bytes.GetData(), bytes.Num()));
    request.requestId = 0;
    TestFalse(TEXT("Reject missing correlation"), request.Serialize(bytes));
    request.requestId = 1;
    request.idToken = FString::ChrN(MAX_FIREBASE_ID_TOKEN_SIZE + 1, 'x');
    TestFalse(TEXT("Reject oversized token"), request.Serialize(bytes));
    FFirebaseLoginResponsePacket response;
    response.requestId = 9;
    response.uid = TEXT("naver_v1_") + FString::ChrN(64, 'a');
    TestTrue(TEXT("Encode verified UID"), response.Serialize(bytes));
    FFirebaseLoginResponsePacket reply;
    TestTrue(TEXT("Decode verified UID"), reply.Deserialize(bytes.GetData(), bytes.Num()));
    TestEqual(TEXT("Reply UID roundtrip"), reply.uid, response.uid);
    response.result = EPacketSendResult::AUTH_REJECTED;
    TestFalse(TEXT("Failure cannot carry UID"), response.Serialize(bytes));
    response.uid.Empty();
    TestTrue(TEXT("Encode auth rejection"), response.Serialize(bytes));
    return true;
}

#endif
