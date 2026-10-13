import pathlib, sys, struct, unittest, zlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from phone_sim import packet
from read_log import records, LogError
class ToolsTests(unittest.TestCase):
    def test_wire_golden_vector(self):
        b=packet(4,42,3,2010,source=1,flags=11,hazards=1,confidence=90)
        self.assertEqual(b.hex(),'5253573104010b012a00000003000000da0700000000000000000000005a0000'+'00'*32)
    def test_token_length(self):
        with self.assertRaises(ValueError):packet(1,1,1,token='short')
        self.assertEqual(len(packet(1,1,1,token='x'*32)),64)
    def test_no_sequence_wrap(self):
        for seq in (0,2**32):
            with self.assertRaises(ValueError):packet(3,1,seq)
    def record(self):
        b=bytearray(48);b[:4]=b'RSM1';struct.pack_into('<I',b,4,48);struct.pack_into('<I',b,44,zlib.crc32(b[:44]));return b
    def test_complete_before_bad_tail(self):
        g=records(self.record()+b'partial');self.assertEqual(next(g)['type'],'RSM1')
        with self.assertRaises(LogError):next(g)
    def test_crc(self):
        b=self.record();b[20]^=1
        with self.assertRaises(LogError):list(records(b))
if __name__=='__main__':unittest.main()
