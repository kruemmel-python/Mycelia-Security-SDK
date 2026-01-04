#include "mycelia_vulkan_compute.h"

#include <jni.h>
#include <vector>

extern "C" JNIEXPORT jlong JNICALL
Java_com_mycelia_security_MyceliaNative_nativeInit(JNIEnv *, jobject) {
    auto *compute = new MyceliaVulkanCompute();
    if (!compute->initialize()) {
        delete compute;
        return 0;
    }
    return reinterpret_cast<jlong>(compute);
}

extern "C" JNIEXPORT void JNICALL
Java_com_mycelia_security_MyceliaNative_nativeRelease(JNIEnv *, jobject, jlong handle) {
    auto *compute = reinterpret_cast<MyceliaVulkanCompute *>(handle);
    if (!compute) {
        return;
    }
    compute->shutdown();
    delete compute;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mycelia_security_MyceliaNative_nativeEncrypt(JNIEnv *env,
                                                      jobject,
                                                      jlong handle,
                                                      jbyteArray input,
                                                      jbyteArray seed) {
    auto *compute = reinterpret_cast<MyceliaVulkanCompute *>(handle);
    if (!compute) {
        return nullptr;
    }

    jsize inputLen = env->GetArrayLength(input);
    jsize seedLen = env->GetArrayLength(seed);

    std::vector<uint8_t> inputBuf(static_cast<size_t>(inputLen));
    std::vector<uint8_t> seedBuf(static_cast<size_t>(seedLen));

    env->GetByteArrayRegion(input, 0, inputLen, reinterpret_cast<jbyte *>(inputBuf.data()));
    env->GetByteArrayRegion(seed, 0, seedLen, reinterpret_cast<jbyte *>(seedBuf.data()));

    std::vector<uint8_t> output;
    if (!compute->encrypt(inputBuf, output, seedBuf)) {
        return nullptr;
    }

    jbyteArray result = env->NewByteArray(static_cast<jsize>(output.size()));
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
                            reinterpret_cast<jbyte *>(output.data()));
    return result;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mycelia_security_MyceliaNative_nativeDecrypt(JNIEnv *env,
                                                      jobject,
                                                      jlong handle,
                                                      jbyteArray input,
                                                      jbyteArray seed) {
    auto *compute = reinterpret_cast<MyceliaVulkanCompute *>(handle);
    if (!compute) {
        return nullptr;
    }

    jsize inputLen = env->GetArrayLength(input);
    jsize seedLen = env->GetArrayLength(seed);

    std::vector<uint8_t> inputBuf(static_cast<size_t>(inputLen));
    std::vector<uint8_t> seedBuf(static_cast<size_t>(seedLen));

    env->GetByteArrayRegion(input, 0, inputLen, reinterpret_cast<jbyte *>(inputBuf.data()));
    env->GetByteArrayRegion(seed, 0, seedLen, reinterpret_cast<jbyte *>(seedBuf.data()));

    std::vector<uint8_t> output;
    if (!compute->decrypt(inputBuf, output, seedBuf)) {
        return nullptr;
    }

    jbyteArray result = env->NewByteArray(static_cast<jsize>(output.size()));
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
                            reinterpret_cast<jbyte *>(output.data()));
    return result;
}
