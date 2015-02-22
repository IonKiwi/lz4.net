using lz4.AnyCPU.loader;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace lz4 {
	public sealed class LZ4Stream : Stream {

		private Stream _innerStream;

		public event EventHandler<LZ4UserDataFrameEventArgs> UserDataFrameRead;

		private void OnUserDataFrameRead(LZ4UserDataFrameEventArgs e) {
			EventHandler<LZ4UserDataFrameEventArgs> handler = UserDataFrameRead;
			if (handler != null) {
				handler(this, e);
			}
		}

		private void HookInternalEvent() {
			LZ4Loader.UserFrameEvent()(_innerStream, (x, y) => OnUserDataFrameRead(y));
		}

		private LZ4Stream() { }

		public static LZ4Stream CreateCompressor(Stream innerStream, LZ4StreamMode streamMode, LZ4FrameBlockMode blockMode = LZ4FrameBlockMode.Linked, LZ4FrameBlockSize blockSize = LZ4FrameBlockSize.Max64KB, LZ4FrameChecksumMode checksumMode = LZ4FrameChecksumMode.Content, long? maxFrameSize = null, bool leaveInnerStreamOpen = false) {
			var s = LZ4Loader.CreateCompressor()(innerStream, streamMode, blockMode, blockSize, checksumMode, maxFrameSize, leaveInnerStreamOpen);
			var r = new LZ4Stream();
			r._innerStream = s;
			return r;
		}

		public static LZ4Stream CreateDecompressor(Stream innerStream, LZ4StreamMode streamMode, bool leaveInnerStreamOpen = false) {
			var s = LZ4Loader.CreateDecompressor()(innerStream, streamMode, leaveInnerStreamOpen);
			var r = new LZ4Stream();
			r._innerStream = s;
			r.HookInternalEvent();
			return r;
		}

		internal long CurrentBlockCount {
			get { return LZ4Loader.CurrentBlockCount()(_innerStream); }
		}

		public long FrameCount {
			get { return LZ4Loader.FrameCount()(_innerStream); }
		}

		public void WriteEndFrame() {
			LZ4Loader.WriteEndFrame()(_innerStream);
		}

		public void WriteUserDataFrame(int id, byte[] buffer, int offset, int count) {
			LZ4Loader.WriteUserDataFrame()(_innerStream, id, buffer, offset, count);
		}

		protected override void Dispose(bool disposing) {
			if (disposing && _callDispose) {
				_innerStream.Dispose();
			}
			base.Dispose(disposing);
		}

		public override void Flush() {
			_innerStream.Flush();
		}

		public override bool CanRead {
			get { return _innerStream.CanRead; }
		}

		public override bool CanSeek {
			get { return _innerStream.CanSeek; }
		}

		public override bool CanTimeout {
			get {
				return _innerStream.CanTimeout;
			}
		}

		public override bool CanWrite {
			get { return _innerStream.CanWrite; }
		}

		public override long Position {
			get {
				return _innerStream.Position;
			}
			set {
				_innerStream.Position = value;
			}
		}

		public override long Length {
			get { return _innerStream.Length; }
		}

		private bool _callDispose = true;
		public override void Close() {
			_innerStream.Close();
			_callDispose = false;
			base.Close(); // will call Dispose(true)
		}

		public override int Read(byte[] buffer, int offset, int count) {
			return _innerStream.Read(buffer, offset, count);
		}

		public override long Seek(long offset, SeekOrigin origin) {
			return _innerStream.Seek(offset, origin);
		}

		public override void SetLength(long value) {
			_innerStream.SetLength(value);
		}

		public override void Write(byte[] buffer, int offset, int count) {
			_innerStream.Write(buffer, offset, count);
		}

		public override int ReadTimeout {
			get {
				return _innerStream.ReadTimeout;
			}
			set {
				_innerStream.ReadTimeout = value;
			}
		}

		public override int WriteTimeout {
			get {
				return _innerStream.WriteTimeout;
			}
			set {
				_innerStream.WriteTimeout = value;
			}
		}

		public override void WriteByte(byte value) {
			_innerStream.WriteByte(value);
		}

		public override int ReadByte() {
			return _innerStream.ReadByte();
		}
	}
}
