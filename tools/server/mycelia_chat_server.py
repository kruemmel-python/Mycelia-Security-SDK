import asyncio
import json
import math
import random
import struct
from collections import defaultdict
from typing import Dict, Set

MAX_FRAME_SIZE = 10 * 1024 * 1024


class SubqgRng:
    def __init__(self, width: int = 32, height: int = 32, iterations: int = 6) -> None:
        self.width = width
        self.height = height
        self.iterations = iterations
        self.count = width * height
        self.random = random.Random()

    def generate_entropy(self, length: int) -> bytes:
        energy = [0.5] * self.count
        pressure = [0.0] * self.count
        gravity = [0.0] * self.count
        magnetism = [0.0] * self.count
        temperature = [0.0] * self.count
        potential = [0.0] * self.count
        drift_x = [0.0] * self.count
        drift_y = [0.0] * self.count
        phase = [0.25] * self.count

        rng_energy = [self.random.random() for _ in range(self.count)]
        rng_phase = [self.random.random() for _ in range(self.count)]
        rng_spin = [self.random.random() for _ in range(self.count)]

        def clamp(v: float, lo: float = -1.0, hi: float = 1.0) -> float:
            return min(hi, max(lo, v))

        def clamp_index(x: int, y: int) -> int:
            cx = min(self.width - 1, max(0, x))
            cy = min(self.height - 1, max(0, y))
            return cy * self.width + cx

        def sample(field: list[float], x: int, y: int) -> float:
            return field[clamp_index(x, y)]

        def laplace(field: list[float], x: int, y: int) -> float:
            return (
                sample(field, x, y)
                + sample(field, x - 1, y)
                + sample(field, x + 1, y)
                + sample(field, x, y - 1)
                + sample(field, x, y + 1)
                - 4.0 * sample(field, x, y)
            )

        for _ in range(self.iterations):
            for idx in range(self.count):
                x = idx % self.width
                y = idx // self.width
                lap_e = laplace(energy, x, y)
                lap_p = laplace(pressure, x, y)
                lap_g = laplace(gravity, x, y)
                lap_m = laplace(magnetism, x, y)
                lap_t = laplace(temperature, x, y)
                lap_v = laplace(potential, x, y)

                noise_e = (rng_energy[idx] - 0.5) * 0.3
                noise_p = (rng_phase[idx] - 0.5) * 0.3
                noise_m = (rng_spin[idx] - 0.5) * 0.3

                e = energy[idx] + 0.10 * lap_e + noise_e
                p = pressure[idx] + 0.08 * lap_p + 0.05 * (energy[idx] - pressure[idx]) + noise_p
                t = temperature[idx] + 0.05 * lap_t + 0.10 * (energy[idx] - temperature[idx])
                v = potential[idx] + 0.04 * lap_v + 0.04 * (pressure[idx] + gravity[idx] - 2.0 * potential[idx])
                g = gravity[idx] + 0.02 * lap_g + 0.08 * (potential[idx] - gravity[idx])
                m = magnetism[idx] + 0.03 * lap_m + 0.02 * (abs(drift_x[idx]) + abs(drift_y[idx])) + noise_m

                e = clamp(e)
                p = clamp(p)
                t = clamp(t)
                v = clamp(v)
                g = clamp(g)
                m = clamp(m)

                grad_ex = 0.5 * (sample(energy, x + 1, y) - sample(energy, x - 1, y))
                grad_ey = 0.5 * (sample(energy, x, y + 1) - sample(energy, x, y - 1))
                drift_x[idx] = 0.95 * drift_x[idx] + 0.05 * grad_ex
                drift_y[idx] = 0.95 * drift_y[idx] + 0.05 * grad_ey

                current_phase = clamp(phase[idx])
                phase_acc = math.asin(current_phase) / math.pi + (noise_p * 0.2)
                phase[idx] = math.sin(phase_acc * math.pi)

                energy[idx] = e
                pressure[idx] = p
                temperature[idx] = t
                potential[idx] = v
                gravity[idx] = g
                magnetism[idx] = m

        data = bytearray()
        for idx in range(self.count):
            mix = (
                energy[idx] * 0.4
                + pressure[idx] * 0.2
                + temperature[idx] * 0.2
                + potential[idx] * 0.2
            )
            data.extend(struct.pack("<f", mix))
        out = bytearray()
        while len(out) < length:
            out.extend(data)
        return bytes(out[:length])

async def read_exact(reader: asyncio.StreamReader, size: int) -> bytes:
    data = await reader.readexactly(size)
    return data


async def read_frame(reader: asyncio.StreamReader) -> bytes:
    prefix = await read_exact(reader, 4)
    length = struct.unpack(">I", prefix)[0]
    if length <= 0 or length > MAX_FRAME_SIZE:
        raise ValueError("Invalid frame length")
    return await read_exact(reader, length)


async def write_frame(writer: asyncio.StreamWriter, payload: bytes) -> None:
    if len(payload) > MAX_FRAME_SIZE:
        raise ValueError("Frame too large")
    writer.write(struct.pack(">I", len(payload)) + payload)
    await writer.drain()


class ChatServer:
    def __init__(self, history_limit: int = 100) -> None:
        self.rooms: Dict[str, Set[asyncio.StreamWriter]] = defaultdict(set)
        self.history: Dict[str, list[bytes]] = defaultdict(list)
        self.room_entropy: Dict[str, bytes] = {}
        self.history_limit = history_limit
        self.lock = asyncio.Lock()
        self.subqg_rng = SubqgRng()

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        room_id = ""
        try:
            join_payload = await read_frame(reader)
            join_message = json.loads(join_payload.decode("utf-8"))
            if join_message.get("type") != "join":
                return
            room_id = join_message.get("roomId")
            if not room_id:
                return
            async with self.lock:
                self.rooms[room_id].add(writer)
                if room_id not in self.room_entropy:
                    self.room_entropy[room_id] = self.subqg_rng.generate_entropy(32)
                history = list(self.history.get(room_id, []))

            for payload in history:
                try:
                    await write_frame(writer, payload)
                except Exception:
                    break

            while True:
                payload = await read_frame(reader)
                message = json.loads(payload.decode("utf-8"))
                message_type = message.get("type")
                if message_type not in {"message", "hello"}:
                    continue
                if message.get("roomId") != room_id:
                    continue
                async with self.lock:
                    stored = self.history[room_id]
                    stored.append(payload)
                    if len(stored) > self.history_limit:
                        del stored[: len(stored) - self.history_limit]
                await self.broadcast(room_id, payload)
        except (asyncio.IncompleteReadError, ConnectionResetError, OSError):
            pass
        except Exception:
            pass
        finally:
            if room_id:
                async with self.lock:
                    if writer in self.rooms.get(room_id, set()):
                        self.rooms[room_id].remove(writer)
                        if not self.rooms[room_id]:
                            del self.rooms[room_id]
                            self.room_entropy.pop(room_id, None)
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionResetError, OSError):
                pass

    async def broadcast(self, room_id: str, payload: bytes) -> None:
        async with self.lock:
            writers = list(self.rooms.get(room_id, []))
        for writer in writers:
            try:
                await write_frame(writer, payload)
            except Exception:
                continue


async def main(host: str = "0.0.0.0", port: int = 8989) -> None:
    server = ChatServer()
    server_coro = await asyncio.start_server(server.handle_client, host, port)
    addresses = ", ".join(str(sock.getsockname()) for sock in server_coro.sockets or [])
    print(f"Mycelia chat server listening on {addresses}")
    async with server_coro:
        await server_coro.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
