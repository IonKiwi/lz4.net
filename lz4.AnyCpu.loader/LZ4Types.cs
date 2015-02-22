using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace lz4 {
	public enum LZ4FrameBlockMode {
		Linked,
		Independent
	}

	public enum LZ4FrameBlockSize {
		Max64KB,
		Max256KB,
		Max1MB,
		Max4MB
	}

	public enum LZ4StreamMode {
		Read,
		Write,
	}

	[Flags]
	public enum LZ4FrameChecksumMode {
		None,
		Content,
		Block
	}

	public sealed class LZ4UserDataFrameEventArgs : EventArgs {
		private byte[] _data;
		private int _id;

		public LZ4UserDataFrameEventArgs(int id, byte[] data) {
			this._id = id;
			this._data = data;
		}

		public byte[] Data {
			get {
				return this._data;
			}
		}

		public int Id {
			get {
				return this._id;
			}
		}
	}
}
