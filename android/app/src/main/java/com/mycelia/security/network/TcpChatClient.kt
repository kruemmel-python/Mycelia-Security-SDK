package com.mycelia.security.network

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.io.ByteArrayInputStream
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager
import java.nio.charset.StandardCharsets
import kotlin.math.min

class TcpChatClient(
    private val host: String,
    private val port: Int,
    private val roomId: String,
    private val tlsEnabled: Boolean,
    private val tlsPinSha256: String,
    private val tlsCaPem: String
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val sendChannel = Channel<ChatPayload>(Channel.BUFFERED)
    private val _incoming = MutableSharedFlow<ChatPayload>(extraBufferCapacity = 64)
    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)

    val incoming = _incoming.asSharedFlow()
    val connectionState = _connectionState.asStateFlow()

    private var runner: Job? = null

    fun start() {
        if (runner != null) return
        runner = scope.launch {
            reconnectLoop()
        }
    }

    suspend fun sendMessage(payload: ChatPayload) {
        sendChannel.send(payload)
    }

    fun stop() {
        runner?.cancel()
        runner = null
        scope.launch {
            sendChannel.close()
        }
    }

    private suspend fun reconnectLoop() {
        var backoffMs = 500L
        while (scope.isActive) {
            try {
                connectOnce()
                backoffMs = 500L
            } catch (ex: Exception) {
                _connectionState.value = ConnectionState.Error(ex.message ?: "Connection error")
                delay(backoffMs)
                backoffMs = min(backoffMs * 2, 8000L)
            }
        }
    }

    private suspend fun connectOnce() = withContext(Dispatchers.IO) {
        val socket = if (tlsEnabled) {
            createTlsSocket()
        } else {
            Socket().apply { connect(InetSocketAddress(host, port), 5000) }
        }
        socket.tcpNoDelay = true
        _connectionState.value = ConnectionState.Connected

        val input = BufferedInputStream(socket.getInputStream())
        val output = BufferedOutputStream(socket.getOutputStream())

        writeFrame(output, ChatPayload.Join(roomId))

        try {
            val readerJob = scope.launch { readLoop(input) }
            val writerJob = scope.launch { writeLoop(output) }
            readerJob.join()
            writerJob.cancel()
        } finally {
            socket.close()
            _connectionState.value = ConnectionState.Disconnected
        }
    }

    private suspend fun readLoop(input: BufferedInputStream) {
        val lengthPrefix = ByteArray(4)
        while (scope.isActive) {
            readExact(input, lengthPrefix)
            val length = Framing.decodeLength(lengthPrefix)
            require(length in 1..Framing.MAX_FRAME_SIZE) { "Invalid frame length" }
            val payload = ByteArray(length)
            readExact(input, payload)
            val jsonText = payload.toString(StandardCharsets.UTF_8)
            val json = JSONObject(jsonText)
            val parsed = ChatPayload.fromJson(json)
            if (parsed != null) {
                _incoming.emit(parsed)
            }
        }
    }

    private suspend fun writeLoop(output: BufferedOutputStream) {
        for (payload in sendChannel) {
            writeFrame(output, payload)
        }
    }

    private fun writeFrame(output: BufferedOutputStream, payload: ChatPayload) {
        val jsonBytes = payload.toJson().toString().toByteArray(StandardCharsets.UTF_8)
        val frame = Framing.encode(jsonBytes)
        output.write(frame)
        output.flush()
    }

    private fun readExact(input: BufferedInputStream, buffer: ByteArray) {
        var offset = 0
        while (offset < buffer.size) {
            val read = input.read(buffer, offset, buffer.size - offset)
            if (read == -1) throw IllegalStateException("EOF")
            offset += read
        }
    }

    private fun createTlsSocket(): SSLSocket {
        val sslContext = SSLContext.getInstance("TLS")
        val baseTrustManager = if (tlsCaPem.isBlank()) {
            defaultTrustManager()
        } else {
            trustManagerFromPem(tlsCaPem)
        }
        val tm = if (tlsPinSha256.isBlank()) {
            baseTrustManager
        } else {
            PinnedTrustManager(baseTrustManager, tlsPinSha256)
        }
        sslContext.init(null, arrayOf(tm), null)
        val factory = sslContext.socketFactory
        val socket = factory.createSocket(host, port) as SSLSocket
        socket.startHandshake()
        return socket
    }

    private fun defaultTrustManager(): X509TrustManager {
        val trustManagers = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
        trustManagers.init(null as KeyStore?)
        return trustManagers.trustManagers.filterIsInstance<X509TrustManager>().first()
    }

    private fun trustManagerFromPem(pem: String): X509TrustManager {
        val cleaned = pem
            .replace("-----BEGIN CERTIFICATE-----", "")
            .replace("-----END CERTIFICATE-----", "")
            .replace("\\s".toRegex(), "")
        val certBytes = android.util.Base64.decode(cleaned, android.util.Base64.DEFAULT)
        val certFactory = CertificateFactory.getInstance("X.509")
        val cert = certFactory.generateCertificate(ByteArrayInputStream(certBytes)) as X509Certificate
        val keyStore = KeyStore.getInstance(KeyStore.getDefaultType()).apply { load(null) }
        keyStore.setCertificateEntry("mycelia_ca", cert)
        val tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
        tmf.init(keyStore)
        return tmf.trustManagers.filterIsInstance<X509TrustManager>().first()
    }

    private class PinnedTrustManager(
        private val delegate: X509TrustManager,
        private val pinHex: String
    ) : X509TrustManager {
        override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) {
            delegate.checkClientTrusted(chain, authType)
        }

        override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {
            delegate.checkServerTrusted(chain, authType)
            if (chain.isEmpty()) throw IllegalStateException("Empty server certificate chain")
            val cert = chain[0]
            val digest = MessageDigest.getInstance("SHA-256").digest(cert.encoded)
            val actual = digest.joinToString("") { "%02x".format(it) }
            if (!actual.equals(pinHex.replace(":", ""), ignoreCase = true)) {
                throw IllegalStateException("TLS pin mismatch")
            }
        }

        override fun getAcceptedIssuers(): Array<X509Certificate> = delegate.acceptedIssuers
    }

    sealed class ConnectionState {
        data object Connected : ConnectionState()
        data object Disconnected : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }
}
