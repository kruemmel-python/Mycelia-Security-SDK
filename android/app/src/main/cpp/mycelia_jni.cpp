#include "mycelia_vulkan_compute.h"

#include <jni.h>
#include <vector>

extern "C" JNIEXPORT jlong JNICALL
Java_com_mycelia_security_MyceliaNative_nativeInit(JNIEnv *env, jobject, jstring shaderDir) {
    const char *dir = env->GetStringUTFChars(shaderDir, nullptr);
    auto *compute = new MyceliaVulkanCompute(dir ? dir : "");
    if (dir) {
        env->ReleaseStringUTFChars(shaderDir, dir);
    }
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

static std::vector<uint8_t> toVector(JNIEnv *env, jbyteArray arr) {
    jsize len = env->GetArrayLength(arr);
    std::vector<uint8_t> buf(static_cast<size_t>(len));
    env->GetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte *>(buf.data()));
    return buf;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mycelia_security_MyceliaNative_nativeEncrypt(JNIEnv *env,
                                                      jobject,
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
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
                            reinterpret_cast<jbyte *>(output.data()));
    return result;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mycelia_security_MyceliaNative_nativeDecrypt(JNIEnv *env,
                                                      jobject,
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
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(output.size()),
                            reinterpret_cast<jbyte *>(output.data()));
    return result;
}
