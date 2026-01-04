#include "mycelia_vulkan_compute.h"

#include <jni.h>
#include <vector>
#include <cstdint>

static std::vector<uint8_t> toVector(JNIEnv *env, jbyteArray arr) {
    if (!arr) {
        return {};
    }
    jsize len = env->GetArrayLength(arr);
    std::vector<uint8_t> buf(static_cast<size_t>(len));
    if (len > 0) {
        env->GetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte *>(buf.data()));
    }
    return buf;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_mycelia_security_crypto_MyceliaNative_nativeInit(JNIEnv *env, jobject /*thiz*/, jstring shaderDir) {
    const char *dir = nullptr;
    if (shaderDir) {
        dir = env->GetStringUTFChars(shaderDir, nullptr);
    }

    auto *compute = new MyceliaVulkanCompute(dir ? dir : "");

    if (shaderDir && dir) {
        env->ReleaseStringUTFChars(shaderDir, dir);
    }

    if (!compute->initialize()) {
        delete compute;
        return 0;
    }
    return reinterpret_cast<jlong>(compute);
}

extern "C" JNIEXPORT void JNICALL
Java_com_mycelia_security_crypto_MyceliaNative_nativeRelease(JNIEnv * /*env*/, jobject /*thiz*/, jlong handle) {
    auto *compute = reinterpret_cast<MyceliaVulkanCompute *>(handle);
    if (!compute) {
        return;
    }
    compute->shutdown();
    delete compute;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mycelia_security_crypto_MyceliaNative_nativeEncrypt(JNIEnv *env,
                                                             jobject /*thiz*/,
                                                             jlong handle,
                                                             jbyteArray input,
                                                             jbyteArray seed,
                                                             jlong streamOffset) {
    auto *compute = reinterpret_cast<MyceliaVulkanCompute *>(handle);
    if (!compute) {
        return nullptr;
    }

    std::vector<uint8_t> inputBuf = toVector(env, input);
    std::vector<uint8_t> seedBuf = toVector(env, seed);

    std::vector<uint8_t> output;
    if (!compute->encrypt(inputBuf, output, seedBuf, static_cast<uint64_t>(streamOffset))) {
        return nullptr;
    }

    jbyteArray result = env->NewByteArray(static_cast<jsize>(output.size()));
    if (result && !output.empty()) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
                                reinterpret_cast<const jbyte *>(output.data()));
    }
    return result;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mycelia_security_crypto_MyceliaNative_nativeDecrypt(JNIEnv *env,
                                                             jobject /*thiz*/,
                                                             jlong handle,
                                                             jbyteArray input,
                                                             jbyteArray seed,
                                                             jlong streamOffset) {
    auto *compute = reinterpret_cast<MyceliaVulkanCompute *>(handle);
    if (!compute) {
        return nullptr;
    }

    std::vector<uint8_t> inputBuf = toVector(env, input);
    std::vector<uint8_t> seedBuf = toVector(env, seed);

    std::vector<uint8_t> output;
    if (!compute->decrypt(inputBuf, output, seedBuf, static_cast<uint64_t>(streamOffset))) {
        return nullptr;
    }

    jbyteArray result = env->NewByteArray(static_cast<jsize>(output.size()));
    if (result && !output.empty()) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
                                reinterpret_cast<const jbyte *>(output.data()));
    }
    return result;
}
