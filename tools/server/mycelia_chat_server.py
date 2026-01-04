import asyncio
import json
import struct
from collections import defaultdict
from typing import Dict, Set

MAX_FRAME_SIZE = 10 * 1024 * 1024


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
        self.history_limit = history_limit
        self.lock = asyncio.Lock()

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
                history = list(self.history.get(room_id, []))

            for payload in history:
                try:
                    await write_frame(writer, payload)
                except Exception:
                    break

            while True:
                payload = await read_frame(reader)
                message = json.loads(payload.decode("utf-8"))
                if message.get("type") != "message":
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
