#include "stdafx.h"
/*
   Source File
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   source repository: https://github.com/IonKiwi/lz4.net
   */

#include "lz4Stream.h"
#include "lz4.h"
#include "lz4hc.h"
#include "xxhash.h"

#define KB *(1 <<10)
#define MB *(1 <<20)

typedef unsigned int        U32;

namespace lz4 {

	LZ4Stream::LZ4Stream() {

	}

	LZ4Stream^ LZ4Stream::CreateCompressor(Stream^ innerStream, LZ4StreamMode streamMode, LZ4FrameBlockMode blockMode, LZ4FrameBlockSize blockSize, LZ4FrameChecksumMode checksumMode, Nullable<long long> maxFrameSize, bool highCompression, bool leaveInnerStreamOpen) {
		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }
		if (maxFrameSize.HasValue && maxFrameSize.Value <= 0) { throw gcnew ArgumentOutOfRangeException("maxFrameSize"); }

		LZ4Stream^ result = gcnew LZ4Stream();
		result->_streamMode = streamMode;
		result->_innerStream = innerStream;
		result->_compressionMode = CompressionMode::Compress;
		result->_checksumMode = checksumMode;
		result->_blockMode = blockMode;
		result->_blockSize = blockSize;
		result->_maxFrameSize = maxFrameSize;
		result->_leaveInnerStreamOpen = leaveInnerStreamOpen;
		result->_highCompression = highCompression;
		result->Init();

		return result;
	}

	LZ4Stream^ LZ4Stream::CreateDecompressor(Stream^ innerStream, LZ4StreamMode streamMode, bool leaveInnerStreamOpen) {
		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }

		LZ4Stream^ result = gcnew LZ4Stream();
		result->_streamMode = streamMode;
		result->_innerStream = innerStream;
		result->_compressionMode = CompressionMode::Decompress;
		result->_leaveInnerStreamOpen = leaveInnerStreamOpen;
		result->Init();

		return result;
	}

	LZ4Stream::~LZ4Stream() {
		if (_compressionMode == CompressionMode::Compress && _streamMode == LZ4StreamMode::Write) { WriteEndFrameInternal(); }

		if (!_leaveInnerStreamOpen) {
			delete _innerStream;
		}

		if (_inputBufferHandle.IsAllocated) { _inputBufferHandle.Free(); _inputBufferPtr = nullptr; }
		if (_outputBufferHandle.IsAllocated) { _outputBufferHandle.Free(); _outputBufferPtr = nullptr; }
		_inputBuffer = nullptr;
		_outputBuffer = nullptr;

		this->!LZ4Stream();
	}

	LZ4Stream::!LZ4Stream() {
		if (_lz4Stream != nullptr) { LZ4_freeStream(_lz4Stream); _lz4Stream = nullptr; }
		if (_lz4HCStream != nullptr) { LZ4_freeStreamHC(_lz4HCStream); _lz4HCStream = nullptr; }
		if (_contentHashState != nullptr) { XXH32_freeState(_contentHashState); _contentHashState = nullptr; }
		if (_lz4DecodeStream != nullptr) { LZ4_freeStreamDecode(_lz4DecodeStream); _lz4DecodeStream = nullptr; }
	}

	void LZ4Stream::Init() {
		if (_compressionMode == CompressionMode::Compress) {
			if (!_highCompression) {
				_lz4Stream = LZ4_createStream();
			}
			else {
				_lz4HCStream = LZ4_createStreamHC();
			}

			switch (_blockSize) {
			case LZ4FrameBlockSize::Max64KB:
				_inputBufferSize = 64 KB;
				_outputBufferSize = 64 KB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				_inputBufferHandle = GCHandle::Alloc(_inputBuffer, GCHandleType::Pinned);
				_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
				_inputBufferPtr = (char *)(void *)_inputBufferHandle.AddrOfPinnedObject();
				_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
				break;
			case LZ4FrameBlockSize::Max256KB:
				_inputBufferSize = 256 KB;
				_outputBufferSize = 256 KB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				_inputBufferHandle = GCHandle::Alloc(_inputBuffer, GCHandleType::Pinned);
				_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
				_inputBufferPtr = (char *)(void *)_inputBufferHandle.AddrOfPinnedObject();
				_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
				break;
			case LZ4FrameBlockSize::Max1MB:
				_inputBufferSize = 1 MB;
				_outputBufferSize = 1 MB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				_inputBufferHandle = GCHandle::Alloc(_inputBuffer, GCHandleType::Pinned);
				_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
				_inputBufferPtr = (char *)(void *)_inputBufferHandle.AddrOfPinnedObject();
				_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
				break;
			case LZ4FrameBlockSize::Max4MB:
				_inputBufferSize = 4 MB;
				_outputBufferSize = 4 MB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				_inputBufferHandle = GCHandle::Alloc(_inputBuffer, GCHandleType::Pinned);
				_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
				_inputBufferPtr = (char *)(void *)_inputBufferHandle.AddrOfPinnedObject();
				_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
				break;
			default:
				throw gcnew NotSupportedException(_blockSize.ToString());
			}

			if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
				_contentHashState = XXH32_createState();
				XXH32_reset(_contentHashState, 0);
			}
		}
		else {
			_lz4DecodeStream = LZ4_createStreamDecode();
			_contentHashState = XXH32_createState();
			XXH32_reset(_contentHashState, 0);
		}
	}

	bool LZ4Stream::Get_CanRead() {
		return _streamMode == LZ4StreamMode::Read;
	}

	bool LZ4Stream::Get_CanSeek() {
		return false;
	}

	bool LZ4Stream::Get_CanWrite() {
		return _streamMode == LZ4StreamMode::Write;
	}

	long long LZ4Stream::Get_Length() {
		return -1;
	}

	long long LZ4Stream::Get_Position() {
		return -1;
	}

	long long LZ4Stream::Seek(long long offset, SeekOrigin origin) {
		throw gcnew NotSupportedException("Seek");
	}

	void LZ4Stream::SetLength(long long value) {
		throw gcnew NotSupportedException("SetLength");
	}

	void LZ4Stream::Flush() {
		if (_compressionMode == CompressionMode::Compress && _streamMode == LZ4StreamMode::Write && _inputBufferOffset > 0) { FlushCurrentBlock(false); }
	}

	void LZ4Stream::WriteEndFrame() {
		if (!(_compressionMode == CompressionMode::Compress && _streamMode == LZ4StreamMode::Write)) { throw gcnew NotSupportedException("Only supported in compress mode with a write mode stream"); }
		WriteEndFrameInternal();
	}

	void LZ4Stream::WriteHeaderData(array<byte>^ buffer, int offset, int count)
	{
		if (_streamMode == LZ4StreamMode::Read)
		{
			Buffer::BlockCopy(buffer, offset, _headerBuffer, _headerBufferSize, count);
			_headerBufferSize += count;
		}
		else
		{
			_innerStream->Write(buffer, offset, count);
		}
	}

	void LZ4Stream::WriteEndFrameInternal() {
		if (!_hasWrittenInitialStartFrame) {
			//WriteEmptyFrame();
			return;
		}

		if (!_hasWrittenStartFrame) {
			return;
			//throw gcnew InvalidOperationException("No start frame was written");
		}

		if (_inputBufferOffset > 0) {
			// flush current block first
			FlushCurrentBlock(true);
		}

		array<byte>^ b = gcnew array<byte>(4);

		// write end mark
		b[0] = 0;
		b[1] = 0;
		b[2] = 0;
		b[3] = 0; // 0x80
		WriteHeaderData(b, 0, b->Length);

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			U32 xxh = XXH32_digest(_contentHashState);
			XXH32_reset(_contentHashState, 0); // reset for next frame

			b[0] = (byte)(xxh & 0xFF);
			b[1] = (byte)((xxh >> 8) & 0xFF);
			b[2] = (byte)((xxh >> 16) & 0xFF);
			b[3] = (byte)((xxh >> 24) & 0xFF);
			WriteHeaderData(b, 0, b->Length);
		}

		// reset the stream
		if (!_highCompression) {
			LZ4_loadDict(_lz4Stream, nullptr, 0);
		}
		else {
			LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
		}

		_hasWrittenStartFrame = false;
	}

	void LZ4Stream::WriteStartFrame() {
		_hasWrittenStartFrame = true;
		_hasWrittenInitialStartFrame = true;
		_frameCount++;
		_blockCount = 0;
		//_ringbufferOffset = 0;

		// write magic
		array<byte>^ magic = gcnew array<byte>(4);
		magic[0] = 0x04;
		magic[1] = 0x22;
		magic[2] = 0x4D;
		magic[3] = 0x18;
		WriteHeaderData(magic, 0, magic->Length);

		// frame descriptor
		array<byte>^ descriptor = gcnew array<byte>(2); // if _storeContentSize +8
		descriptor[0] = 0;
		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			descriptor[0] |= 0x4;
		}

		//if (_storeContentSize) {
		//	descriptor[0] |= 0x8;
		//}

		if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
			descriptor[0] |= 0x10;
		}

		if (_blockMode == LZ4FrameBlockMode::Independent) {
			descriptor[0] |= 0x20;
		}

		descriptor[0] |= 0x40; // version 01

		descriptor[1] = 0;
		if (_blockSize == LZ4FrameBlockSize::Max64KB) {
			descriptor[1] |= (4 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max256KB) {
			descriptor[1] |= (5 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max1MB) {
			descriptor[1] |= (6 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max4MB) {
			descriptor[1] |= (7 << 4);
		}
		else {
			throw gcnew NotImplementedException();
		}

		//if (_storeContentSize) {
		//	unsigned long long contentsize = 0; // we cant support this, as the contentsize is not known in advance
		//	descriptor[2] = (byte)(contentsize & 0xFF);
		//	descriptor[3] = (byte)((contentsize >> 8) & 0xFF);
		//	descriptor[4] = (byte)((contentsize >> 16) & 0xFF);
		//	descriptor[5] = (byte)((contentsize >> 24) & 0xFF);
		//	descriptor[6] = (byte)((contentsize >> 32) & 0xFF);
		//	descriptor[7] = (byte)((contentsize >> 40) & 0xFF);
		//	descriptor[8] = (byte)((contentsize >> 48) & 0xFF);
		//	descriptor[9] = (byte)((contentsize >> 56) & 0xFF);
		//}

		pin_ptr<byte> descriptorPtr = &descriptor[0];
		U32 xxh = XXH32(descriptorPtr, descriptor->Length, 0);

		WriteHeaderData(descriptor, 0, descriptor->Length);
		array<Byte> ^checksumByte = gcnew array<Byte>(1);
		checksumByte[0] = (xxh >> 8) & 0xFF;
		WriteHeaderData(checksumByte, 0, 1);
	}

	void LZ4Stream::WriteEmptyFrame() {
		if (_compressionMode != CompressionMode::Compress) { throw gcnew NotSupportedException("Only supported in compress mode"); }

		if (_hasWrittenStartFrame || _hasWrittenInitialStartFrame) {
			throw gcnew InvalidOperationException("should not have happend, hasWrittenStartFrame: " + _hasWrittenStartFrame + ", hasWrittenInitialStartFrame: " + _hasWrittenInitialStartFrame);
		}

		// write magic
		array<byte>^ magic = gcnew array<byte>(4);
		magic[0] = 0x04;
		magic[1] = 0x22;
		magic[2] = 0x4D;
		magic[3] = 0x18;
		WriteHeaderData(magic, 0, magic->Length);

		// frame descriptor
		array<byte>^ descriptor = gcnew array<byte>(2);
		descriptor[0] = 0;
		if (_blockMode == LZ4FrameBlockMode::Independent) { descriptor[0] |= 0x20; }
		descriptor[0] |= 0x40; // version 01

		descriptor[1] = 0;
		if (_blockSize == LZ4FrameBlockSize::Max64KB) {
			descriptor[1] |= (4 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max256KB) {
			descriptor[1] |= (5 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max1MB) {
			descriptor[1] |= (6 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max4MB) {
			descriptor[1] |= (7 << 4);
		}
		else {
			throw gcnew NotImplementedException();
		}

		pin_ptr<byte> descriptorPtr = &descriptor[0];
		U32 xxh = XXH32(descriptorPtr, descriptor->Length, 0);

		WriteHeaderData(descriptor, 0, descriptor->Length);
		array<Byte> ^checksumByte = gcnew array<Byte>(1);
		checksumByte[0] = (xxh >> 8) & 0xFF;
		WriteHeaderData(checksumByte, 0, 1);

		array<byte>^ endMarker = gcnew array<byte>(4);
		WriteHeaderData(endMarker, 0, endMarker->Length);

		_hasWrittenInitialStartFrame = true;
		_frameCount++;
	}

	void LZ4Stream::WriteUserDataFrame(int id, array<byte>^ buffer, int offset, int count) {
		if (!(_compressionMode == CompressionMode::Compress && _streamMode == LZ4StreamMode::Write)) { throw gcnew NotSupportedException("Only supported in compress mode with a write mode stream"); }
		WriteUserDataFrameInternal(id, buffer, offset, count);
	}

	void LZ4Stream::WriteUserDataFrameInternal(int id, array<byte>^ buffer, int offset, int count) {
		if (id < 0 || id > 15) { throw gcnew ArgumentOutOfRangeException("id"); }
		else if (buffer == nullptr) { throw gcnew ArgumentNullException("buffer"); }
		else if (count < 0) { throw gcnew ArgumentOutOfRangeException("count"); }
		else if (offset < 0) { throw gcnew ArgumentOutOfRangeException("offset"); }
		else if (offset + count > buffer->Length) { throw gcnew ArgumentOutOfRangeException("offset + count"); }

		if (!_hasWrittenInitialStartFrame) {
			// write empty frame according to spec recommendation
			WriteEmptyFrame();
		}

		if (_hasWrittenStartFrame) {
			WriteEndFrameInternal();
		}

		// write magic
		array<byte>^ magic = gcnew array<byte>(4);
		magic[0] = (0x50 + id);
		magic[1] = 0x2A;
		magic[2] = 0x4D;
		magic[3] = 0x18;
		_innerStream->Write(magic, 0, magic->Length);

		// write size
		array<byte>^ b = gcnew array<byte>(4);
		b[0] = (byte)((unsigned int)count & 0xFF);
		b[1] = (byte)(((unsigned int)count >> 8) & 0xFF);
		b[2] = (byte)(((unsigned int)count >> 16) & 0xFF);
		b[3] = (byte)(((unsigned int)count >> 24) & 0xFF);
		_innerStream->Write(b, 0, b->Length);

		// write data
		_innerStream->Write(buffer, offset, count);

		_frameCount++;
	}

	void LZ4Stream::FlushCurrentBlock(bool suppressEndFrame) {

		char* inputBufferPtr = &_inputBufferPtr[_ringbufferOffset];
		char* outputBufferPtr = &_outputBufferPtr[0];

		if (!_hasWrittenStartFrame) {
			WriteStartFrame();
		}

		if (_blockMode == LZ4FrameBlockMode::Independent || _blockCount == 0) {
			// reset the stream { create independently compressed blocks }
			if (!_highCompression) {
				LZ4_loadDict(_lz4Stream, nullptr, 0);
			}
			else {
				LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
			}
		}

		int targetSize;
		bool isCompressed;

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			XXH_errorcode status = XXH32_update(_contentHashState, inputBufferPtr, _inputBufferOffset);
			if (status != XXH_errorcode::XXH_OK) {
				throw gcnew Exception("Failed to update content checksum");
			}
		}

		int outputBytes;
		if (!_highCompression) {
			outputBytes = LZ4_compress_fast_continue(_lz4Stream, inputBufferPtr, outputBufferPtr, _inputBufferOffset, _outputBufferSize, 1);
		}
		else {
			outputBytes = LZ4_compress_HC_continue(_lz4HCStream, inputBufferPtr, outputBufferPtr, _inputBufferOffset, _outputBufferSize);
		}

		if (outputBytes == 0) {
			// compression failed or output is too large

			// reset the stream
			//if (!_highCompression) {
			//	LZ4_loadDict(_lz4Stream, nullptr, 0);
			//}
			//else {
			//	LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
			//}

			Buffer::BlockCopy(_inputBuffer, _ringbufferOffset, _outputBuffer, 0, _inputBufferOffset);
			targetSize = _inputBufferOffset;
			isCompressed = false;
		}
		else if (outputBytes >= _inputBufferOffset) {
			// compressed size is bigger than input size

			// reset the stream
			//if (!_highCompression) {
			//	LZ4_loadDict(_lz4Stream, nullptr, 0);
			//}
			//else {
			//	LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
			//}

			Buffer::BlockCopy(_inputBuffer, _ringbufferOffset, _outputBuffer, 0, _inputBufferOffset);
			targetSize = _inputBufferOffset;
			isCompressed = false;
		}
		else if (outputBytes < 0) {
			throw gcnew Exception("Compress failed");
		}
		else {
			targetSize = outputBytes;
			isCompressed = true;
		}

		array<byte>^ b = gcnew array<byte>(4);
		b[0] = (byte)((unsigned int)targetSize & 0xFF);
		b[1] = (byte)(((unsigned int)targetSize >> 8) & 0xFF);
		b[2] = (byte)(((unsigned int)targetSize >> 16) & 0xFF);
		b[3] = (byte)(((unsigned int)targetSize >> 24) & 0xFF);

		if (!isCompressed) {
			b[3] |= 0x80;
		}

		_innerStream->Write(b, 0, b->Length);
		_innerStream->Write(_outputBuffer, 0, targetSize);

		if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
			void* targetPtr = &_outputBufferPtr[0];
			U32 xxh = XXH32(targetPtr, targetSize, 0);

			b[0] = (byte)(xxh & 0xFF);
			b[1] = (byte)((xxh >> 8) & 0xFF);
			b[2] = (byte)((xxh >> 16) & 0xFF);
			b[3] = (byte)((xxh >> 24) & 0xFF);
			_innerStream->Write(b, 0, b->Length);
		}

		_inputBufferOffset = 0; // reset before calling WriteEndFrame() !!
		_blockCount++;

		if (!suppressEndFrame && _maxFrameSize.HasValue && _blockCount >= _maxFrameSize.Value) {
			WriteEndFrameInternal();
		}

		// update ringbuffer offset
		_ringbufferOffset += _inputBufferSize;
		// wraparound the ringbuffer offset
		if (_ringbufferOffset > _inputBufferSize) _ringbufferOffset = 0;
	}

	bool LZ4Stream::GetFrameInfo() {

		if (_hasFrameInfo) {
			throw gcnew Exception("should not have happend, _hasFrameInfo: " + _hasFrameInfo);
		}

		array<byte>^ magic = gcnew array<byte>(4);
		int bytesRead = _innerStream->Read(magic, 0, magic->Length);
		if (bytesRead == 0) {
			return false;
		}
		else if (bytesRead != magic->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		_blockCount = 0;
		if (magic[0] == 0x04 && magic[1] == 0x22 && magic[2] == 0x4D && magic[3] == 0x18) {
			// lz4 frame
			_frameCount++;

			// reset state
			XXH32_reset(_contentHashState, 0);
			_blockSize = LZ4FrameBlockSize::Max64KB;
			_blockMode = LZ4FrameBlockMode::Linked;
			_checksumMode = LZ4FrameChecksumMode::None;
			_contentSize = 0;
			_outputBufferOffset = 0;
			_outputBufferBlockSize = 0;
			//_ringbufferOffset = 0;

			// read frame descriptor
			array<byte>^ descriptor = gcnew array<byte>(2);
			bytesRead = _innerStream->Read(descriptor, 0, descriptor->Length);
			if (bytesRead != descriptor->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			// verify version
			if (!((descriptor[0] & 0x40) == 0x40 || (descriptor[0] & 0x60) == 0x60) || (descriptor[0] & 0x80) != 0x00) {
				throw gcnew Exception("Unexpected frame version");
			}
			else if ((descriptor[0] & 0x01) != 0x00) {
				throw gcnew Exception("Predefined dictionaries are not supported");
			}
			else if ((descriptor[0] & 0x02) != 0x00) {
				// reserved value
				throw gcnew Exception("Header contains unexpected value");
			}

			if ((descriptor[0] & 0x04) == 0x04) {
				_checksumMode = _checksumMode | LZ4FrameChecksumMode::Content;
			}
			bool hasContentSize = false;
			if ((descriptor[0] & 0x08) == 0x08) {
				hasContentSize = true;
			}
			if ((descriptor[0] & 0x10) == 0x10) {
				_checksumMode = _checksumMode | LZ4FrameChecksumMode::Block;
			}
			if ((descriptor[0] & 0x20) == 0x20) {
				_blockMode = LZ4FrameBlockMode::Independent;
			}

			if ((descriptor[1] & 0x0F) != 0x00) {
				// reserved value
				throw gcnew Exception("Header contains unexpected value");
			}
			else if ((descriptor[1] & 0x80) != 0x00) {
				// reserved value
				throw gcnew Exception("Header contains unexpected value");
			}

			int blockSizeId = (descriptor[1] & 0x70) >> 4;
			if (blockSizeId == 4) {
				_blockSize = LZ4FrameBlockSize::Max64KB;
				_inputBufferSize = 64 KB;
				_outputBufferSize = 64 KB;
			}
			else if (blockSizeId == 5) {
				_blockSize = LZ4FrameBlockSize::Max256KB;
				_inputBufferSize = 256 KB;
				_outputBufferSize = 256 KB;
			}
			else if (blockSizeId == 6) {
				_blockSize = LZ4FrameBlockSize::Max1MB;
				_inputBufferSize = 1 MB;
				_outputBufferSize = 1 MB;
			}
			else if (blockSizeId == 7) {
				_blockSize = LZ4FrameBlockSize::Max4MB;
				_inputBufferSize = 4 MB;
				_outputBufferSize = 4 MB;
			}
			else {
				throw gcnew Exception("Unsupported block size: " + blockSizeId);
			}

			if (hasContentSize) {
				byte tmp1 = descriptor[0], tmp2 = descriptor[1];
				descriptor = gcnew array<byte>(2 + 8);
				descriptor[0] = tmp1;
				descriptor[1] = tmp2;
				bytesRead = _innerStream->Read(descriptor, 2, 8);
				if (bytesRead != 8) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

				_contentSize = 0;
				for (int i = 9; i >= 2; i--) {
					_contentSize |= ((unsigned long long)descriptor[i] << (i * 8));
				}
			}

			// read checksum
			array<byte>^ checksum = gcnew array<byte>(1);
			bytesRead = _innerStream->Read(checksum, 0, checksum->Length);
			if (bytesRead != checksum->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }
			// verify checksum
			pin_ptr<byte> descriptorPtr = &descriptor[0];
			U32 xxh = XXH32(descriptorPtr, descriptor->Length, 0);
			byte checksumByte = (xxh >> 8) & 0xFF;
			if (checksum[0] != checksumByte) {
				throw gcnew Exception("Frame checksum is invalid");
			}

			// resize buffers
			if (_inputBuffer == nullptr || _inputBuffer->Length != _inputBufferSize) {
				if (_inputBufferHandle.IsAllocated) { _inputBufferHandle.Free(); _inputBufferPtr = nullptr; }
				_inputBuffer = gcnew array<byte>(_inputBufferSize);
				_inputBufferHandle = GCHandle::Alloc(_inputBuffer, GCHandleType::Pinned);
				_inputBufferPtr = (char *)(void *)_inputBufferHandle.AddrOfPinnedObject();
			}
			if (_outputBuffer == nullptr || _outputBuffer->Length != 2 * _outputBufferSize) {
				if (_outputBufferHandle.IsAllocated) { _outputBufferHandle.Free(); _outputBufferPtr = nullptr; }
				_outputBuffer = gcnew array<byte>(2 * _outputBufferSize);
				_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
				_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
			}

			_hasFrameInfo = true;
			return true;
		}
		else if (magic[0] >= 0x50 && magic[0] <= 0x5f && magic[1] == 0x2A && magic[2] == 0x4D && magic[3] == 0x18) {
			// skippable frame
			_frameCount++;

			// read frame size
			array<byte>^ b = gcnew array<byte>(4);
			bytesRead = _innerStream->Read(b, 0, b->Length);
			if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			unsigned int frameSize = 0;
			for (int i = b->Length - 1; i >= 0; i--) {
				frameSize |= ((unsigned int)b[i] << (i * 8));
			}

			array<byte>^ userData = gcnew array<byte>(frameSize);
			bytesRead = _innerStream->Read(userData, 0, userData->Length);
			if (bytesRead != userData->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			int id = (magic[0] & 0xF);

			LZ4UserDataFrameEventArgs^ e = gcnew LZ4UserDataFrameEventArgs(id, userData);
			UserDataFrameRead(this, e);

			// read next frame header
			return GetFrameInfo();
		}
		else {
			throw gcnew Exception("lz4 stream is corrupt");
		}
	}

	bool LZ4Stream::AcquireNextBlock() {
		if (!_hasFrameInfo) {
			if (!GetFrameInfo()) {
				return false;
			}
		}

		array<byte>^ b = gcnew array<byte>(4);

		// read block size
		int bytesRead = _innerStream->Read(b, 0, b->Length);
		if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		bool isCompressed = true;
		if ((b[3] & 0x80) == 0x80) {
			isCompressed = false;
			b[3] &= 0x7F;
		}

		unsigned int blockSize = 0;
		for (int i = b->Length - 1; i >= 0; i--) {
			blockSize |= ((unsigned int)b[i] << (i * 8));
		}

		if (blockSize > (unsigned int)_outputBufferSize) {
			throw gcnew Exception("Block size exceeds maximum block size");
		}

		if (blockSize == 0) {
			// end marker

			//_outputBufferSize = 0;
			//_outputBufferOffset = 0;
			//_outputBufferBlockSize = 0;
			//_inputBufferSize = 0;
			//_inputBufferOffset = 0;
			_hasFrameInfo = false;

			if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
				// calculate hash
				U32 xxh = XXH32_digest(_contentHashState);

				// read hash
				b = gcnew array<byte>(4);
				bytesRead = _innerStream->Read(b, 0, b->Length);
				if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

				if (b[0] != (byte)(xxh & 0xFF) ||
					b[1] != (byte)((xxh >> 8) & 0xFF) ||
					b[2] != (byte)((xxh >> 16) & 0xFF) ||
					b[3] != (byte)((xxh >> 24) & 0xFF)) {
					throw gcnew Exception("Content checksum did not match");
				}
			}

			return AcquireNextBlock();
		}

		// read block data
		bytesRead = _innerStream->Read(_inputBuffer, 0, blockSize);
		if (bytesRead != blockSize) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		_blockCount++;

		if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
			// read block checksum
			b = gcnew array<byte>(4);
			bytesRead = _innerStream->Read(b, 0, b->Length);
			if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			unsigned int checksum = 0;
			for (int i = b->Length - 1; i >= 0; i--) {
				checksum |= ((unsigned int)b[i] << (i * 8));
			}
			// verify checksum
			void* targetPtr = &_inputBufferPtr[0];
			U32 xxh = XXH32(targetPtr, blockSize, 0);
			if (checksum != xxh) {
				throw gcnew Exception("Block checksum did not match");
			}
		}

		int currentRingbufferOffset = _ringbufferOffset;

		// preserve previously compressed block data (for LZ4_decompress_safe_continue dictionary [LZ4FrameBlockMode::Linked])
		// update ringbuffer offset
		_ringbufferOffset += _outputBufferSize;
		// wraparound the ringbuffer offset
		if (_ringbufferOffset > _outputBufferSize) _ringbufferOffset = 0;

		if (!isCompressed) {
			Buffer::BlockCopy(_inputBuffer, 0, _outputBuffer, _ringbufferOffset, blockSize);
			_outputBufferBlockSize = blockSize;
		}
		else {
			char* inputPtr = &_inputBufferPtr[0];
			char* outputPtr = &_outputBufferPtr[_ringbufferOffset];
			char* dict = &_outputBufferPtr[currentRingbufferOffset];
			int status;
			if (_blockCount > 1) {
				status = LZ4_setStreamDecode(_lz4DecodeStream, dict, _outputBufferSize);
			}
			else {
				status = LZ4_setStreamDecode(_lz4DecodeStream, nullptr, 0);
			}
			if (status != 1) {
				throw gcnew Exception("LZ4_setStreamDecode failed");
			}
			int decompressedSize = LZ4_decompress_safe_continue(_lz4DecodeStream, inputPtr, outputPtr, blockSize, _outputBufferSize);
			if (decompressedSize <= 0) {
				throw gcnew Exception("Decompress failed");
			}
			_outputBufferBlockSize = decompressedSize;
		}

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			void* contentPtr = &_outputBufferPtr[_ringbufferOffset];
			XXH_errorcode status = XXH32_update(_contentHashState, contentPtr, _outputBufferBlockSize);
			if (status != XXH_errorcode::XXH_OK) {
				throw gcnew Exception("Failed to update content checksum");
			}
		}

		_outputBufferOffset = 0;

		return true;
	}

	void LZ4Stream::CompressNextBlock() {

		// write at least one start frame
		//if (!_hasWrittenInitialStartFrame) { WriteStartFrame(); }

		int chunk = _inputBufferSize - _inputBufferOffset;
		if (chunk == 0) { throw gcnew Exception("should not have happend, Read(): compress, chunk == 0"); }

		// compress 1 block
		bool streamEnd = false;
		int bytesRead;
		do
		{
			bytesRead = _innerStream->Read(_inputBuffer, _ringbufferOffset + _inputBufferOffset, chunk);
			if (bytesRead == 0)
			{
				_isCompressed = true;
				break;
			}
			else
			{
				_inputBufferOffset += bytesRead;
				chunk -= bytesRead;
			}
		} while (chunk > 0);

		if (_inputBufferOffset == 0) {
			return;
		}

		_headerBufferSize = 0;
		_outputBufferOffset = 0;

		char* inputBufferPtr = &_inputBufferPtr[_ringbufferOffset];
		char* outputBufferPtr = &_outputBufferPtr[0];

		if (!_hasWrittenStartFrame) {
			WriteStartFrame();
		}

		if (_blockMode == LZ4FrameBlockMode::Independent || _blockCount == 0) {
			// reset the stream { create independently compressed blocks }
			if (!_highCompression) {
				LZ4_loadDict(_lz4Stream, nullptr, 0);
			}
			else {
				LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
			}
		}

		int targetSize;
		bool isCompressed;

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			XXH_errorcode status = XXH32_update(_contentHashState, inputBufferPtr, _inputBufferOffset);
			if (status != XXH_errorcode::XXH_OK) {
				throw gcnew Exception("Failed to update content checksum");
			}
		}

		int outputBytes;
		if (!_highCompression) {
			outputBytes = LZ4_compress_fast_continue(_lz4Stream, inputBufferPtr, outputBufferPtr, _inputBufferOffset, _outputBufferSize, 1);
		}
		else {
			outputBytes = LZ4_compress_HC_continue(_lz4HCStream, inputBufferPtr, outputBufferPtr, _inputBufferOffset, _outputBufferSize);
		}
		if (outputBytes == 0) {
			// compression failed or output is too large

			// reset the stream
			//if (!_highCompression) {
			//	LZ4_loadDict(_lz4Stream, nullptr, 0);
			//}
			//else {
			//	LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
			//}

			Buffer::BlockCopy(_inputBuffer, _ringbufferOffset, _outputBuffer, 0, _inputBufferOffset);
			targetSize = _inputBufferOffset;
			isCompressed = false;
		}
		else if (outputBytes >= _inputBufferOffset) {
			// compressed size is bigger than input size

			// reset the stream
			//if (!_highCompression) {
			//	LZ4_loadDict(_lz4Stream, nullptr, 0);
			//}
			//else {
			//	LZ4_loadDictHC(_lz4HCStream, nullptr, 0);
			//}

			Buffer::BlockCopy(_inputBuffer, _ringbufferOffset, _outputBuffer, 0, _inputBufferOffset);
			targetSize = _inputBufferOffset;
			isCompressed = false;
		}
		else if (outputBytes < 0) {
			throw gcnew Exception("Compress failed");
		}
		else {
			targetSize = outputBytes;
			isCompressed = true;
		}

		array<byte>^ b = gcnew array<byte>(4);
		_headerBuffer[_headerBufferSize++] = (byte)((unsigned int)targetSize & 0xFF);
		_headerBuffer[_headerBufferSize++] = (byte)(((unsigned int)targetSize >> 8) & 0xFF);
		_headerBuffer[_headerBufferSize++] = (byte)(((unsigned int)targetSize >> 16) & 0xFF);
		_headerBuffer[_headerBufferSize++] = (byte)(((unsigned int)targetSize >> 24) & 0xFF);

		if (!isCompressed) {
			_headerBuffer[_headerBufferSize - 1] |= 0x80;
		}

		_outputBufferBlockSize = targetSize;
		_currentMode = 1;
	}

	int LZ4Stream::CompressData(array<Byte>^ buffer, int offset, int count)
	{
		// _currentMode == 0 -> frame start
		// _currentMode == 1 -> copy header data
		// _currentMode == 2 -> block data
		// _currentMode == 3 -> copy header data after block

		int consumed = 0, total = 0;
		do {
			offset += consumed;
			count -= consumed;
			consumed = 0;

			if (_currentMode == 0)
			{
				if (!_isCompressed) {
					CompressNextBlock();
				}
				else if (_hasWrittenStartFrame) {
					WriteEndFrameInternal();
					_currentMode = 4;
				}
				else {
					return total;
				}
			}
			else if (_currentMode == 1)
			{
				if (_headerBufferSize == 0) { throw gcnew Exception("should not have happend, Read(): compress, _headerBufferSize == 0"); }
				int chunk = consumed = Math::Min(_headerBufferSize - _outputBufferOffset, count);
				Buffer::BlockCopy(_headerBuffer, _outputBufferOffset, buffer, offset, chunk);
				_outputBufferOffset += chunk;
				if (_outputBufferOffset == _headerBufferSize) {
					_outputBufferOffset = 0;
					_currentMode = 2;
				}
			}
			else if (_currentMode == 2)
			{
				if (_outputBufferBlockSize == 0) { throw gcnew Exception("should not have happend, Read(): compress, _outputBufferBlockSize == 0"); }
				int chunk = consumed = Math::Min(_outputBufferBlockSize - _outputBufferOffset, count);
				Buffer::BlockCopy(_outputBuffer, _outputBufferOffset, buffer, offset, chunk);
				_outputBufferOffset += chunk;
				if (_outputBufferOffset == _outputBufferBlockSize) {
					_inputBufferOffset = 0; // reset before calling WriteEndFrame() !!
					_blockCount++;
					_currentMode = 3;
				}
			}
			else if (_currentMode == 3)
			{
				_currentMode = 0;

				_headerBufferSize = 0;
				_outputBufferOffset = 0;

				if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {

					void* targetPtr = &_outputBufferPtr[0];
					U32 xxh = XXH32(targetPtr, _outputBufferBlockSize, 0);

					_headerBuffer[_headerBufferSize++] = (byte)(xxh & 0xFF);
					_headerBuffer[_headerBufferSize++] = (byte)((xxh >> 8) & 0xFF);
					_headerBuffer[_headerBufferSize++] = (byte)((xxh >> 16) & 0xFF);
					_headerBuffer[_headerBufferSize++] = (byte)((xxh >> 24) & 0xFF);
					_currentMode = 4;
				}

				if (_maxFrameSize.HasValue && _blockCount >= _maxFrameSize.Value) {
					WriteEndFrameInternal();
					_currentMode = 4;
				}

				// update ringbuffer offset
				_ringbufferOffset += _inputBufferSize;
				// wraparound the ringbuffer offset
				if (_ringbufferOffset > _inputBufferSize) _ringbufferOffset = 0;
			}
			else if (_currentMode == 4)
			{
				if (_headerBufferSize == 0) { throw gcnew Exception("should not have happend, Read(): compress, _headerBufferSize == 0"); }
				int chunk = consumed = Math::Min(_headerBufferSize - _outputBufferOffset, count);
				Buffer::BlockCopy(_headerBuffer, _outputBufferOffset, buffer, offset, chunk);
				_outputBufferOffset += chunk;
				if (_outputBufferOffset == _headerBufferSize) {
					_outputBufferOffset = 0;
					_headerBufferSize = 0;
					_currentMode = 0;
				}
			}
			else {
				throw gcnew Exception("should not have happend, Read(): compress, _currentmode == " + _currentMode);
			}

			total += consumed;
		} while (!_interactiveRead && consumed < count);

		return total;
	}

	int LZ4Stream::ReadByte() {
		if (_streamMode != LZ4StreamMode::Read) { throw gcnew NotSupportedException("Read"); }

		if (_compressionMode == CompressionMode::Decompress) {
			if (_outputBufferOffset >= _outputBufferBlockSize && !AcquireNextBlock()) {
				return -1; // end of stream
			}
			return _outputBuffer[_ringbufferOffset + _outputBufferOffset++];
		}
		else {
			array<Byte> ^data = gcnew array<Byte>(1);
			int x = CompressData(data, 0, 1);
			if (x == 0) {
				return -1; // stream end
			}
			return data[0];
		}
	}

	int LZ4Stream::Read(array<byte>^ buffer, int offset, int count) {
		if (_streamMode != LZ4StreamMode::Read) { throw gcnew NotSupportedException("Read"); }
		else if (buffer == nullptr) { throw gcnew ArgumentNullException("buffer"); }
		else if (offset < 0) { throw gcnew ArgumentOutOfRangeException("offset"); }
		else if (count < 0) { throw gcnew ArgumentOutOfRangeException("count"); }
		else if (offset + count > buffer->Length) { throw gcnew ArgumentOutOfRangeException("offset+count"); }

		if (_compressionMode == CompressionMode::Decompress) {
			int total = 0;
			while (count > 0)
			{
				int chunk = Math::Min(count, _outputBufferBlockSize - _outputBufferOffset);
				if (chunk > 0)
				{
					Buffer::BlockCopy(_outputBuffer, _ringbufferOffset + _outputBufferOffset, buffer, offset, chunk);

					_outputBufferOffset += chunk;
					offset += chunk;
					count -= chunk;
					total += chunk;
					if (_interactiveRead) {
						break;
					}
				}
				else
				{
					if (!AcquireNextBlock()) break;
				}
			}
			return total;
		}
		else {
			return CompressData(buffer, offset, count);
		}
	}

	void LZ4Stream::WriteByte(byte value) {
		if (_streamMode != LZ4StreamMode::Write) { throw gcnew NotSupportedException("Write"); }

		if (_compressionMode == CompressionMode::Compress)
		{
			if (!_hasWrittenStartFrame) { WriteStartFrame(); }

			if (_inputBufferOffset >= _inputBufferSize)
			{
				FlushCurrentBlock(false);
			}

			_inputBuffer[_ringbufferOffset + _inputBufferOffset++] = value;
		}
		else
		{
			array<Byte> ^b = gcnew array<Byte>(1);
			b[0] = value;
			DecompressData(b, 0, 1);
		}
	}

	void LZ4Stream::Write(array<byte>^ buffer, int offset, int count) {
		if (_streamMode != LZ4StreamMode::Write) { throw gcnew NotSupportedException("Write"); }
		else if (buffer == nullptr) { throw gcnew ArgumentNullException("buffer"); }
		else if (offset < 0) { throw gcnew ArgumentOutOfRangeException("offset"); }
		else if (count < 0) { throw gcnew ArgumentOutOfRangeException("count"); }
		else if (offset + count > buffer->Length) { throw gcnew ArgumentOutOfRangeException("offset+count"); }

		if (count == 0) { return; }

		if (_compressionMode == CompressionMode::Compress)
		{
			if (!_hasWrittenStartFrame) { WriteStartFrame(); }

			while (count > 0)
			{
				int chunk = Math::Min(count, _inputBufferSize - _inputBufferOffset);
				if (chunk > 0)
				{
					Buffer::BlockCopy(buffer, offset, _inputBuffer, _ringbufferOffset + _inputBufferOffset, chunk);

					offset += chunk;
					count -= chunk;
					_inputBufferOffset += chunk;
				}
				else
				{
					FlushCurrentBlock(false);
				}
			}
		}
		else
		{
			DecompressData(buffer, offset, count);
		}
	}

	void LZ4Stream::DecompressData(array<Byte>^ data, int offset, int count)
	{
		int consumed = 0;
		do {
			offset += consumed;
			count -= consumed;
			consumed = 0;

			if (_currentMode >= 0 && _currentMode <= 4) {
				consumed = DecompressHeader(data, offset, count);
			}
			else if (_currentMode == 5) {
				// user data frame
				int l = _outputBuffer->Length;
				int remaining = l - _outputBufferOffset;
				int chunk = consumed = Math::Min(remaining, count);
				Buffer::BlockCopy(data, offset, _outputBuffer, _outputBufferOffset, chunk);
				_outputBufferOffset += chunk;
				remaining = l - _outputBufferOffset;
				if (remaining == 0) {
					int id = (_headerBuffer[0] & 0xF);

					array<Byte>^ userData = gcnew array<Byte>(l);
					Buffer::BlockCopy(_outputBuffer, 0, userData, 0, l);
					LZ4UserDataFrameEventArgs^ e = gcnew LZ4UserDataFrameEventArgs(id, userData);
					UserDataFrameRead(this, e);

					_headerBufferSize = 0;
					_currentMode = 0;
				}
			}
			else if (_currentMode >= 6 && _currentMode <= 10) {
				// lz4 frame
				consumed = DecompressBlock(data, offset, count);
			}
			else {
				throw gcnew Exception("should not have happend, Write(): decompress, _currentmode == " + _currentMode);
			}
		} while (consumed < count);
	}

	int LZ4Stream::DecompressBlock(array<Byte>^ data, int offset, int count)
	{
		int consumed = 0;
		if (_currentMode == 6) {
			for (int i = _headerBufferSize, ii = 0; i < 4 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 4) {

				_isCompressed = true;
				if ((_headerBuffer[3] & 0x80) == 0x80) {
					_isCompressed = false;
					_headerBuffer[3] &= 0x7F;
				}

				unsigned int blockSize = 0;
				for (int i = 3; i >= 0; i--) {
					blockSize |= ((unsigned int)_headerBuffer[i] << (i * 8));
				}

				if (blockSize > (unsigned int)_outputBufferSize) {
					throw gcnew Exception("Block size exceeds maximum block size");
				}

				if (blockSize == 0) {
					// end marker

					if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
						_headerBufferSize = 0;
						_currentMode = 7;
					}
					else {
						_headerBufferSize = 0;
						_currentMode = 0;
					}
					return consumed;
				}

				_targetBufferSize = blockSize;
				_inputBufferOffset = 0;
				_currentMode = 8;
			}
		}
		else if (_currentMode == 7) {
			for (int i = _headerBufferSize, ii = 0; i < 4 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 4) {

				// calculate hash
				U32 xxh = XXH32_digest(_contentHashState);

				if (_headerBuffer[0] != (byte)(xxh & 0xFF) ||
					_headerBuffer[1] != (byte)((xxh >> 8) & 0xFF) ||
					_headerBuffer[2] != (byte)((xxh >> 16) & 0xFF) ||
					_headerBuffer[3] != (byte)((xxh >> 24) & 0xFF)) {
					throw gcnew Exception("Content checksum did not match");
				}

				_headerBufferSize = 0;
				_currentMode = 0;
			}
		}
		else if (_currentMode == 8) {

			int remaining = _targetBufferSize - _inputBufferOffset;

			int chunk = consumed = Math::Min(remaining, count);
			Buffer::BlockCopy(data, offset, _inputBuffer, _inputBufferOffset, chunk);
			_inputBufferOffset += chunk;
			remaining = _targetBufferSize - _inputBufferOffset;
			if (remaining == 0) {

				_blockCount++;

				if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
					_headerBufferSize = 0;
					_currentMode = 9;
				}
				else {
					_currentMode = 10;
				}
			}
		}
		else if (_currentMode == 9) {
			// block checksum
			for (int i = _headerBufferSize, ii = 0; i < 4 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 4) {
				unsigned int checksum = 0;
				for (int i = 3; i >= 0; i--) {
					checksum |= ((unsigned int)_headerBuffer[i] << (i * 8));
				}
				// verify checksum
				void* targetPtr = &_inputBufferPtr[0];
				U32 xxh = XXH32(targetPtr, _targetBufferSize, 0);
				if (checksum != xxh) {
					throw gcnew Exception("Block checksum did not match");
				}

				_currentMode = 10;
			}
		}
		else if (_currentMode == 10) {
			int currentRingbufferOffset = _ringbufferOffset;

			// preserve previously compressed block data (for LZ4_decompress_safe_continue dictionary [LZ4FrameBlockMode::Linked])
			// update ringbuffer offset
			_ringbufferOffset += _outputBufferSize;
			// wraparound the ringbuffer offset
			if (_ringbufferOffset > _outputBufferSize) _ringbufferOffset = 0;

			if (!_isCompressed) {
				Buffer::BlockCopy(_inputBuffer, 0, _outputBuffer, _ringbufferOffset, _targetBufferSize);
				_outputBufferBlockSize = _targetBufferSize;
			}
			else {
				char* inputPtr = &_inputBufferPtr[0];
				char* outputPtr = &_outputBufferPtr[_ringbufferOffset];
				char* dict = &_outputBufferPtr[currentRingbufferOffset];
				int status;
				if (_blockCount > 1) {
					status = LZ4_setStreamDecode(_lz4DecodeStream, dict, _outputBufferSize);
				}
				else {
					status = LZ4_setStreamDecode(_lz4DecodeStream, nullptr, 0);
				}
				if (status != 1) {
					throw gcnew Exception("LZ4_setStreamDecode failed");
				}
				int decompressedSize = LZ4_decompress_safe_continue(_lz4DecodeStream, inputPtr, outputPtr, _targetBufferSize, _outputBufferSize);
				if (decompressedSize <= 0) {
					throw gcnew Exception("Decompress failed");
				}
				_outputBufferBlockSize = decompressedSize;
			}

			_innerStream->Write(_outputBuffer, _ringbufferOffset, _outputBufferBlockSize);

			if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
				void* contentPtr = &_outputBufferPtr[_ringbufferOffset];
				XXH_errorcode status = XXH32_update(_contentHashState, contentPtr, _outputBufferBlockSize);
				if (status != XXH_errorcode::XXH_OK) {
					throw gcnew Exception("Failed to update content checksum");
				}
			}

			_headerBufferSize = 0;
			_outputBufferOffset = 0;
			_currentMode = 6;
		}
		else {
			throw gcnew Exception("should not have happend, Write(): decompress, _currentmode == " + _currentMode);
		}

		return consumed;
	}

	int LZ4Stream::DecompressHeader(array<Byte>^ data, int offset, int count)
	{
		// _currentMode == 0 -> no data
		// _currentMode == 1 -> lz4 frame magic
		// _currentMode == 2 -> user frame magic
		// _currentMode == 3 -> checksum
		// _currentMode == 4 -> lz4 content size
		// _currentMode == 5 -> user frame size		
		// _currentMode == 6 -> frame data
		// _currentMode == 7 -> content checksum
		// _currentMode == 8 -> block data
		// _currentMode == 9 -> block checksum
		// _currentMode == 10 -> decompress block

		int consumed = 0;
		if (_currentMode == 0) {
			for (int i = _headerBufferSize, ii = 0; i < 4 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 4) {
				_blockCount = 0;
				if (_headerBuffer[0] == 0x04 && _headerBuffer[1] == 0x22 && _headerBuffer[2] == 0x4D && _headerBuffer[3] == 0x18) {
					// expect 2 byte descriptor
					_currentMode = 1;
				}
				else if (_headerBuffer[0] >= 0x50 && _headerBuffer[0] <= 0x5f && _headerBuffer[1] == 0x2A && _headerBuffer[2] == 0x4D && _headerBuffer[3] == 0x18) {
					_currentMode = 2;
				}
				else {
					throw gcnew Exception("Invalid magic: 0x" + _headerBuffer[0].ToString("x2") + _headerBuffer[1].ToString("x2") + _headerBuffer[2].ToString("x2") + _headerBuffer[3].ToString("x2"));
				}
			}
		}
		else if (_currentMode == 1)
		{
			for (int i = _headerBufferSize, ii = 0; i < 6 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 6) {
				_frameCount++;

				// reset state
				XXH32_reset(_contentHashState, 0);
				_blockSize = LZ4FrameBlockSize::Max64KB;
				_blockMode = LZ4FrameBlockMode::Linked;
				_checksumMode = LZ4FrameChecksumMode::None;
				_contentSize = 0;
				_outputBufferOffset = 0;
				_outputBufferBlockSize = 0;
				//_ringbufferOffset = 0;

				// verify version
				if (!((_headerBuffer[4] & 0x40) == 0x40 || (_headerBuffer[4] & 0x60) == 0x60) || (_headerBuffer[4] & 0x80) != 0x00) {
					throw gcnew Exception("Unexpected frame version");
				}
				else if ((_headerBuffer[4] & 0x01) != 0x00) {
					throw gcnew Exception("Predefined dictionaries are not supported");
				}
				else if ((_headerBuffer[4] & 0x02) != 0x00) {
					// reserved value
					throw gcnew Exception("Header contains unexpected value");
				}

				if ((_headerBuffer[4] & 0x04) == 0x04) {
					_checksumMode = _checksumMode | LZ4FrameChecksumMode::Content;
				}
				bool hasContentSize = false;
				if ((_headerBuffer[4] & 0x08) == 0x08) {
					hasContentSize = true;
				}
				if ((_headerBuffer[4] & 0x10) == 0x10) {
					_checksumMode = _checksumMode | LZ4FrameChecksumMode::Block;
				}
				if ((_headerBuffer[4] & 0x20) == 0x20) {
					_blockMode = LZ4FrameBlockMode::Independent;
				}

				if ((_headerBuffer[5] & 0x0F) != 0x00) {
					// reserved value
					throw gcnew Exception("Header contains unexpected value");
				}
				else if ((_headerBuffer[5] & 0x80) != 0x00) {
					// reserved value
					throw gcnew Exception("Header contains unexpected value");
				}

				int blockSizeId = (_headerBuffer[5] & 0x70) >> 4;
				if (blockSizeId == 4) {
					_blockSize = LZ4FrameBlockSize::Max64KB;
					_inputBufferSize = 64 KB;
					_outputBufferSize = 64 KB;
				}
				else if (blockSizeId == 5) {
					_blockSize = LZ4FrameBlockSize::Max256KB;
					_inputBufferSize = 256 KB;
					_outputBufferSize = 256 KB;
				}
				else if (blockSizeId == 6) {
					_blockSize = LZ4FrameBlockSize::Max1MB;
					_inputBufferSize = 1 MB;
					_outputBufferSize = 1 MB;
				}
				else if (blockSizeId == 7) {
					_blockSize = LZ4FrameBlockSize::Max4MB;
					_inputBufferSize = 4 MB;
					_outputBufferSize = 4 MB;
				}
				else {
					throw gcnew Exception("Unsupported block size: " + blockSizeId);
				}

				// resize buffers
				if (_inputBuffer == nullptr || _inputBuffer->Length != _inputBufferSize) {
					if (_inputBufferHandle.IsAllocated) { _inputBufferHandle.Free(); _inputBufferPtr = nullptr; }
					_inputBuffer = gcnew array<byte>(_inputBufferSize);
					_inputBufferHandle = GCHandle::Alloc(_inputBuffer, GCHandleType::Pinned);
					_inputBufferPtr = (char *)(void *)_inputBufferHandle.AddrOfPinnedObject();
				}
				if (_outputBuffer == nullptr || _outputBuffer->Length != 2 * _outputBufferSize) {
					if (_outputBufferHandle.IsAllocated) { _outputBufferHandle.Free(); _outputBufferPtr = nullptr; }
					_outputBuffer = gcnew array<byte>(2 * _outputBufferSize);
					_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
					_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
				}

				if (hasContentSize) {
					// expect 8 more bytes
					_currentMode = 4;
				}
				else {
					_currentMode = 3;
				}
			}
		}
		else if (_currentMode == 4)
		{
			for (int i = _headerBufferSize, ii = 0; i < 8 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 8) {

				_contentSize = 0;
				for (int i = 9; i >= 2; i--) {
					_contentSize |= ((unsigned long long)_headerBuffer[i] << (i * 8));
				}

				_currentMode = 3;
			}
		}
		else if (_currentMode == 3)
		{
			_headerBuffer[_headerBufferSize++] = data[offset];

			// verify checksum
			pin_ptr<byte> descriptorPtr = &_headerBuffer[4];
			U32 xxh = XXH32(descriptorPtr, _headerBufferSize - 5, 0);
			byte checksumByte = (xxh >> 8) & 0xFF;
			if (_headerBuffer[_headerBufferSize - 1] != checksumByte) {
				throw gcnew Exception("Frame checksum is invalid");
			}

			_headerBufferSize = 0;
			_currentMode = 6;
			consumed = 1;
		}
		else if (_currentMode == 2)
		{
			for (int i = _headerBufferSize, ii = 0; i < 8 && ii < count; i++, ii++) {
				_headerBuffer[i] = data[offset + ii];
				_headerBufferSize++;
				consumed++;
			}

			if (_headerBufferSize == 8) {

				_frameCount++;

				unsigned int frameSize = 0;
				for (int i = _headerBufferSize - 1; i >= 4; i--) {
					frameSize |= ((unsigned int)_headerBuffer[i] << (i * 8));
				}

				_outputBufferSize = frameSize;
				_outputBufferOffset = 0;
				if (_outputBufferHandle.IsAllocated) { _outputBufferHandle.Free(); _outputBufferPtr = nullptr; }
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				_outputBufferHandle = GCHandle::Alloc(_outputBuffer, GCHandleType::Pinned);
				_outputBufferPtr = (char *)(void *)_outputBufferHandle.AddrOfPinnedObject();
				_currentMode = 5;
			}
		}

		return consumed;
	}
}
