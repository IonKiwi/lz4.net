/*
   Header File
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

#pragma once

#include "lz4.h"
#include "xxhash.h"

using namespace System;
using namespace System::IO;
using namespace System::IO::Compression;

namespace lz4 {

	public enum class LZ4FrameBlockSize {
		Max64KB,
		Max256KB,
		Max1MB,
		Max4MB,
	};

	public enum class LZ4FrameBlockMode {
		Linked,
		Independent,
	};

	public enum class LZ4StreamMode {
		Read,
		Write,
	};

	[FlagsAttribute]
	public enum class LZ4FrameChecksumMode {
		None = 0x0,
		Content = 0x1,
		Block = 0x2,
	};

	public ref class LZ4UserDataFrameEventArgs sealed : EventArgs
	{
	private:
		typedef unsigned char byte;

		int _id;
		array<byte>^ _data;
	public:
		LZ4UserDataFrameEventArgs(int id, array<byte>^ data) { _id = id; _data = data; }

		property int Id { int get() { return _id; }; };
		property array<byte>^ Data { array<byte>^ get() { return _data; }; };
	};

	public ref class LZ4Stream : Stream
	{
	private:
		typedef unsigned char byte;

		LZ4Stream();

		Stream^ _innerStream;

		array<Byte> ^_headerBuffer = gcnew array<Byte>(23);
		int _headerBufferSize = 0;
		int _currentMode = 0;

		int _targetBufferSize = 0;
		bool _isCompressed = false;

		CompressionMode _compressionMode;
		LZ4FrameBlockSize _blockSize = LZ4FrameBlockSize::Max64KB;
		LZ4FrameBlockMode _blockMode = LZ4FrameBlockMode::Linked;
		LZ4FrameChecksumMode _checksumMode = LZ4FrameChecksumMode::None;
		LZ4StreamMode _streamMode;
		Nullable<long long> _maxFrameSize = Nullable<long long>();
		bool _leaveInnerStreamOpen;
		bool _hasWrittenStartFrame = false;
		bool _hasWrittenInitialStartFrame = false;
		bool _hasFrameInfo = false;
		unsigned long long _contentSize = 0;
		long long _frameCount = 0;
		bool _interactiveRead = false;

		array<byte>^ _inputBuffer = nullptr;
		array<byte>^ _outputBuffer = nullptr;
		int _outputBufferSize = 0;
		int _outputBufferOffset = 0;
		int _outputBufferBlockSize = 0;
		int _inputBufferSize = 0;
		int _inputBufferOffset = 0;
		int _ringbufferOffset = 0;
		long long _blockCount = 0;

		void Init();
		void WriteEmptyFrame();
		void WriteStartFrame();
		void FlushCurrentBlock(bool suppressEndFrame);
		bool GetFrameInfo();
		bool AcquireNextBlock();
		
		int DecompressBlock(array<Byte>^ data, int offset, int count);
		void DecompressData(array<Byte>^ data, int offset, int count);
		int DecompressHeader(array<Byte>^ data, int offset, int count);

		LZ4_stream_t *_lz4Stream = nullptr;
		LZ4_streamDecode_t *_lz4DecodeStream = nullptr;
		XXH32_state_t *_contentHashState = nullptr;

		bool Get_CanRead();
		bool Get_CanSeek();
		bool Get_CanWrite();
		long long Get_Length();
		long long Get_Position();

		void CompressNextBlock();
		int CompressData(array<byte>^ buffer, int offset, int count);
		void WriteHeaderData(array<byte>^ buffer, int offset, int count);

		void WriteEndFrameInternal();
		void WriteUserDataFrameInternal(int id, array<byte>^ buffer, int offset, int count);

	internal:
		property long long CurrentBlockCount {
			long long get() {
				return _blockCount;
			};
		}

	public:
		~LZ4Stream();
		!LZ4Stream();

		static LZ4Stream^ CreateCompressor(Stream^ innerStream, LZ4StreamMode streamMode, LZ4FrameBlockMode blockMode, LZ4FrameBlockSize blockSize, LZ4FrameChecksumMode checksumMode, Nullable<long long> maxFrameSize, bool leaveInnerStreamOpen);
		static LZ4Stream^ CreateDecompressor(Stream^ innerStream, LZ4StreamMode streamMode, bool leaveInnerStreamOpen);

		void WriteEndFrame();
		void WriteUserDataFrame(int id, array<byte>^ buffer, int offset, int count);

		event EventHandler<LZ4UserDataFrameEventArgs^>^ UserDataFrameRead;

		property bool InteractiveRead {
			bool get() {
				return _interactiveRead;
			}
			void set(bool value) {
				_interactiveRead = value;
			}
		}

		property long long FrameCount {
			long long get() {
				return _frameCount;
			};
		}

		property virtual bool CanRead {
			bool get() override {
				return Get_CanRead();
			};
		};
		property virtual bool CanSeek {
			bool get() override {
				return Get_CanSeek();
			}
		}
		property virtual bool CanWrite {
			bool get() override {
				return Get_CanWrite();
			}
		}
		property virtual long long Length {
			long long get() override {
				return Get_Length();
			}
		}
		property virtual long long Position {
			long long get() override {
				return Get_Position();
			}
			void set(long long value) override {
				throw gcnew NotSupportedException("SetPosition");
			}
		}
		virtual void Flush() override;
		virtual int ReadByte() override;
		virtual int Read(array<byte>^ buffer, int offset, int count) override;
		virtual long long Seek(long long offset, SeekOrigin origin) override;
		virtual void SetLength(long long value) override;
		virtual void WriteByte(byte value) override;
		virtual void Write(array<byte>^ buffer, int offset, int count) override;
	};
}
