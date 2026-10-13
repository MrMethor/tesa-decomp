"""Minimal 8086/80186 real-mode interpreter.

Only meant to run small, self-contained code such as an EXE packer's
decompression stub. Unsupported opcodes raise immediately so nothing is
silently mis-emulated.
"""

AX, CX, DX, BX, SP, BP, SI, DI = range(8)
ES, CS, SS, DS = range(4)


class EmuError(Exception):
    pass


class CPU:
    def __init__(self, mem):
        self.m = mem                      # bytearray(1 MiB)
        self.r = [0] * 8
        self.s = [0] * 4
        self.ip = 0
        self.cf = self.zf = self.sf = self.of = self.pf = self.af = 0
        self.df = 0
        self.ifl = 1
        self.int_handler = None           # fn(cpu, intno) -> bool handled
        self.far_jump_hook = None         # fn(cpu, cs, ip) called after far transfer
        self.write_hook = None            # fn(linear, size)
        self.halted = False
        self.steps = 0

    # ---- memory -------------------------------------------------------
    @staticmethod
    def lin(seg, off):
        return ((seg << 4) + (off & 0xFFFF)) & 0xFFFFF

    def rb(self, seg, off):
        return self.m[self.lin(seg, off)]

    def rw(self, seg, off):
        return self.rb(seg, off) | (self.rb(seg, off + 1) << 8)

    def wb(self, seg, off, v):
        a = self.lin(seg, off)
        self.m[a] = v & 0xFF
        if self.write_hook:
            self.write_hook(a, 1)

    def ww(self, seg, off, v):
        self.wb(seg, off, v)
        self.wb(seg, (off + 1) & 0xFFFF, v >> 8)

    def fetch8(self):
        v = self.rb(self.s[CS], self.ip)
        self.ip = (self.ip + 1) & 0xFFFF
        return v

    def fetch16(self):
        lo = self.fetch8()
        return lo | (self.fetch8() << 8)

    def fetchs8(self):
        v = self.fetch8()
        return v - 256 if v & 0x80 else v

    # ---- registers ------------------------------------------------------
    def getr8(self, i):
        return (self.r[i & 3] >> 8) & 0xFF if i & 4 else self.r[i & 3] & 0xFF

    def setr8(self, i, v):
        v &= 0xFF
        if i & 4:
            self.r[i & 3] = (self.r[i & 3] & 0x00FF) | (v << 8)
        else:
            self.r[i & 3] = (self.r[i & 3] & 0xFF00) | v

    def push(self, v):
        self.r[SP] = (self.r[SP] - 2) & 0xFFFF
        self.ww(self.s[SS], self.r[SP], v)

    def pop(self):
        v = self.rw(self.s[SS], self.r[SP])
        self.r[SP] = (self.r[SP] + 2) & 0xFFFF
        return v

    # ---- flags ----------------------------------------------------------
    def getflags(self):
        return (self.cf | 2 | (self.pf << 2) | (self.af << 4) | (self.zf << 6) |
                (self.sf << 7) | (self.ifl << 9) | (self.df << 10) | (self.of << 11) | 0xF000)

    def setflags(self, f):
        self.cf = f & 1
        self.pf = (f >> 2) & 1
        self.af = (f >> 4) & 1
        self.zf = (f >> 6) & 1
        self.sf = (f >> 7) & 1
        self.ifl = (f >> 9) & 1
        self.df = (f >> 10) & 1
        self.of = (f >> 11) & 1

    def szp(self, v, w):
        mask = 0xFFFF if w else 0xFF
        v &= mask
        self.zf = int(v == 0)
        self.sf = int(bool(v & (0x8000 if w else 0x80)))
        self.pf = int(bin(v & 0xFF).count("1") % 2 == 0)
        return v

    def alu(self, op, a, b, w):
        mask = 0xFFFF if w else 0xFF
        top = 0x8000 if w else 0x80
        if op in (0, 2):                                  # add / adc
            c = self.cf if op == 2 else 0
            res = a + b + c
            self.cf = int(res > mask)
            self.of = int(bool((~(a ^ b) & (a ^ res)) & top))
            self.af = int(((a ^ b ^ res) & 0x10) != 0)
            return self.szp(res, w)
        if op in (3, 5, 7):                               # sbb / sub / cmp
            c = self.cf if op == 3 else 0
            res = a - b - c
            self.cf = int(res < 0)
            self.of = int(bool(((a ^ b) & (a ^ res)) & top))
            self.af = int(((a ^ b ^ res) & 0x10) != 0)
            return self.szp(res, w)
        if op == 1:
            res = a | b
        elif op == 4:
            res = a & b
        elif op == 6:
            res = a ^ b
        else:
            raise EmuError("bad alu op")
        self.cf = self.of = 0
        return self.szp(res, w)

    def shift(self, op, v, cnt, w):
        bits = 16 if w else 8
        mask = (1 << bits) - 1
        top = 1 << (bits - 1)
        cnt &= 0x1F
        if cnt == 0:
            return v
        for _ in range(cnt):
            if op == 0:      # rol
                self.cf = (v >> (bits - 1)) & 1
                v = ((v << 1) | self.cf) & mask
            elif op == 1:    # ror
                self.cf = v & 1
                v = (v >> 1) | (self.cf << (bits - 1))
            elif op == 2:    # rcl
                nc = (v >> (bits - 1)) & 1
                v = ((v << 1) | self.cf) & mask
                self.cf = nc
            elif op == 3:    # rcr
                nc = v & 1
                v = (v >> 1) | (self.cf << (bits - 1))
                self.cf = nc
            elif op in (4, 6):  # shl/sal
                self.cf = (v >> (bits - 1)) & 1
                v = (v << 1) & mask
            elif op == 5:    # shr
                self.cf = v & 1
                v >>= 1
            elif op == 7:    # sar
                self.cf = v & 1
                v = (v >> 1) | (v & top)
        if op in (0, 2):
            self.of = ((v >> (bits - 1)) & 1) ^ self.cf
        elif op in (1, 3):
            self.of = ((v >> (bits - 1)) ^ (v >> (bits - 2))) & 1
        elif op in (4, 6):
            self.of = ((v >> (bits - 1)) & 1) ^ self.cf
        elif op == 5:
            self.of = 0
        elif op == 7:
            self.of = 0
        if op >= 4:
            self.szp(v, w)
        return v

    # ---- modrm ----------------------------------------------------------
    def modrm(self):
        b = self.fetch8()
        mod, reg, rm = b >> 6, (b >> 3) & 7, b & 7
        if mod == 3:
            return reg, ("r", rm)
        r = self.r
        bases = [(r[BX] + r[SI]), (r[BX] + r[DI]), (r[BP] + r[SI]), (r[BP] + r[DI]),
                 r[SI], r[DI], r[BP], r[BX]]
        seg = SS if rm in (2, 3) or (rm == 6 and mod != 0) else DS
        if mod == 0 and rm == 6:
            off = self.fetch16()
        else:
            off = bases[rm]
            if mod == 1:
                off += self.fetchs8()
            elif mod == 2:
                off += self.fetch16()
        if self.seg_ovr is not None:
            seg = self.seg_ovr
        return reg, ("m", self.s[seg], off & 0xFFFF)

    def rd(self, loc, w):
        if loc[0] == "r":
            return self.r[loc[1]] if w else self.getr8(loc[1])
        return self.rw(loc[1], loc[2]) if w else self.rb(loc[1], loc[2])

    def wr(self, loc, w, v):
        if loc[0] == "r":
            if w:
                self.r[loc[1]] = v & 0xFFFF
            else:
                self.setr8(loc[1], v)
        elif w:
            self.ww(loc[1], loc[2], v)
        else:
            self.wb(loc[1], loc[2], v)

    # ---- string ops -----------------------------------------------------
    def string_op(self, op, rep):
        w = op & 1
        size = 2 if w else 1
        d = -size if self.df else size
        src_seg = self.s[self.seg_ovr] if self.seg_ovr is not None else self.s[DS]
        while True:
            if rep and self.r[CX] == 0:
                break
            base = op & 0xFE
            if base == 0xA4:      # movs
                v = self.rw(src_seg, self.r[SI]) if w else self.rb(src_seg, self.r[SI])
                if w:
                    self.ww(self.s[ES], self.r[DI], v)
                else:
                    self.wb(self.s[ES], self.r[DI], v)
                self.r[SI] = (self.r[SI] + d) & 0xFFFF
                self.r[DI] = (self.r[DI] + d) & 0xFFFF
            elif base == 0xAA:    # stos
                if w:
                    self.ww(self.s[ES], self.r[DI], self.r[AX])
                else:
                    self.wb(self.s[ES], self.r[DI], self.r[AX])
                self.r[DI] = (self.r[DI] + d) & 0xFFFF
            elif base == 0xAC:    # lods
                if w:
                    self.r[AX] = self.rw(src_seg, self.r[SI])
                else:
                    self.setr8(0, self.rb(src_seg, self.r[SI]))
                self.r[SI] = (self.r[SI] + d) & 0xFFFF
            elif base == 0xA6:    # cmps
                a = self.rw(src_seg, self.r[SI]) if w else self.rb(src_seg, self.r[SI])
                b = self.rw(self.s[ES], self.r[DI]) if w else self.rb(self.s[ES], self.r[DI])
                self.alu(7, a, b, w)
                self.r[SI] = (self.r[SI] + d) & 0xFFFF
                self.r[DI] = (self.r[DI] + d) & 0xFFFF
            elif base == 0xAE:    # scas
                b = self.rw(self.s[ES], self.r[DI]) if w else self.rb(self.s[ES], self.r[DI])
                self.alu(7, self.r[AX] if w else self.getr8(0), b, w)
                self.r[DI] = (self.r[DI] + d) & 0xFFFF
            if not rep:
                break
            self.r[CX] = (self.r[CX] - 1) & 0xFFFF
            if base in (0xA6, 0xAE):
                if rep == 0xF3 and not self.zf:
                    break
                if rep == 0xF2 and self.zf:
                    break

    def far_to(self, cs, ip):
        self.s[CS] = cs
        self.ip = ip
        if self.far_jump_hook:
            self.far_jump_hook(self, cs, ip)

    def cond(self, c):
        t = [self.of, self.cf, self.zf, self.cf or self.zf, self.sf, self.pf,
             self.sf != self.of, self.zf or (self.sf != self.of)][c >> 1]
        return bool(t) ^ bool(c & 1)

    # ---- main step ------------------------------------------------------
    def step(self):
        self.steps += 1
        self.seg_ovr = None
        rep = 0
        start_cs, start_ip = self.s[CS], self.ip
        while True:
            op = self.fetch8()
            if op in (0x26, 0x2E, 0x36, 0x3E):
                self.seg_ovr = (op >> 3) & 3
            elif op in (0xF2, 0xF3):
                rep = op
            elif op == 0xF0:
                pass
            else:
                break
        r = self.r

        if op < 0x40 and (op & 7) < 6:                     # ALU group
            alu_op = op >> 3
            form = op & 7
            w = form & 1
            if form < 4:
                reg, loc = self.modrm()
                rv = r[reg] if w else self.getr8(reg)
                if form & 2:   # reg <- reg op r/m
                    res = self.alu(alu_op, rv, self.rd(loc, w), w)
                    if alu_op != 7:
                        self.wr(("r", reg), w, res)
                else:          # r/m <- r/m op reg
                    res = self.alu(alu_op, self.rd(loc, w), rv, w)
                    if alu_op != 7:
                        self.wr(loc, w, res)
            else:
                imm = self.fetch16() if w else self.fetch8()
                res = self.alu(alu_op, r[AX] if w else self.getr8(0), imm, w)
                if alu_op != 7:
                    self.wr(("r", AX), w, res)
        elif op in (0x06, 0x0E, 0x16, 0x1E):
            self.push(self.s[op >> 3])
        elif op in (0x07, 0x17, 0x1F):
            self.s[op >> 3] = self.pop()
        elif 0x40 <= op <= 0x4F:
            i = op & 7
            cf = self.cf
            r[i] = self.alu(0 if op < 0x48 else 5, r[i], 1, 1)
            self.cf = cf
        elif 0x50 <= op <= 0x57:
            v = r[op & 7]
            self.push(v)
        elif 0x58 <= op <= 0x5F:
            r[op & 7] = self.pop()
        elif op == 0x60:   # pusha
            sp = r[SP]
            for i in range(8):
                self.push(sp if i == SP else r[i])
        elif op == 0x61:   # popa
            for i in reversed(range(8)):
                v = self.pop()
                if i != SP:
                    r[i] = v
        elif op == 0x68:
            self.push(self.fetch16())
        elif op == 0x6A:
            self.push(self.fetchs8() & 0xFFFF)
        elif 0x70 <= op <= 0x7F:
            d = self.fetchs8()
            if self.cond(op & 0xF):
                self.ip = (self.ip + d) & 0xFFFF
        elif op in (0x80, 0x81, 0x82, 0x83):
            w = op & 1
            reg, loc = self.modrm()
            if op == 0x81:
                imm = self.fetch16()
            elif op == 0x83:
                imm = self.fetchs8() & 0xFFFF
            else:
                imm = self.fetch8()
            res = self.alu(reg, self.rd(loc, w), imm, w)
            if reg != 7:
                self.wr(loc, w, res)
        elif op in (0x84, 0x85):
            w = op & 1
            reg, loc = self.modrm()
            self.alu(4, self.rd(loc, w), r[reg] if w else self.getr8(reg), w)
        elif op in (0x86, 0x87):
            w = op & 1
            reg, loc = self.modrm()
            a = self.rd(loc, w)
            self.wr(loc, w, r[reg] if w else self.getr8(reg))
            self.wr(("r", reg), w, a)
        elif 0x88 <= op <= 0x8B:
            w = op & 1
            reg, loc = self.modrm()
            if op & 2:
                self.wr(("r", reg), w, self.rd(loc, w))
            else:
                self.wr(loc, w, r[reg] if w else self.getr8(reg))
        elif op == 0x8C:
            reg, loc = self.modrm()
            self.wr(loc, 1, self.s[reg & 3])
        elif op == 0x8D:
            reg, loc = self.modrm()
            r[reg] = loc[2]
        elif op == 0x8E:
            reg, loc = self.modrm()
            self.s[reg & 3] = self.rd(loc, 1)
        elif op == 0x8F:
            reg, loc = self.modrm()
            self.wr(loc, 1, self.pop())
        elif op == 0x90:
            pass
        elif 0x91 <= op <= 0x97:
            i = op & 7
            r[AX], r[i] = r[i], r[AX]
        elif op == 0x98:
            al = r[AX] & 0xFF
            r[AX] = al | (0xFF00 if al & 0x80 else 0)
        elif op == 0x99:
            r[DX] = 0xFFFF if r[AX] & 0x8000 else 0
        elif op == 0x9A:
            ip = self.fetch16()
            cs = self.fetch16()
            self.push(self.s[CS])
            self.push(self.ip)
            self.far_to(cs, ip)
        elif op == 0x9C:
            self.push(self.getflags())
        elif op == 0x9D:
            self.setflags(self.pop())
        elif op == 0x9E:
            self.setflags((self.getflags() & 0xFF00) | (r[AX] >> 8))
        elif op == 0x9F:
            self.setr8(4, self.getflags() & 0xFF)
        elif 0xA0 <= op <= 0xA3:
            w = op & 1
            off = self.fetch16()
            seg = self.s[self.seg_ovr if self.seg_ovr is not None else DS]
            loc = ("m", seg, off)
            if op & 2:
                self.wr(loc, w, r[AX] if w else self.getr8(0))
            else:
                self.wr(("r", AX), w, self.rd(loc, w))
        elif op in (0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF):
            self.string_op(op, rep)
        elif op in (0xA8, 0xA9):
            w = op & 1
            imm = self.fetch16() if w else self.fetch8()
            self.alu(4, r[AX] if w else self.getr8(0), imm, w)
        elif 0xB0 <= op <= 0xB7:
            self.setr8(op & 7, self.fetch8())
        elif 0xB8 <= op <= 0xBF:
            r[op & 7] = self.fetch16()
        elif op in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
            w = op & 1
            reg, loc = self.modrm()
            if op in (0xC0, 0xC1):
                cnt = self.fetch8()
            elif op in (0xD0, 0xD1):
                cnt = 1
            else:
                cnt = r[CX] & 0xFF
            self.wr(loc, w, self.shift(reg, self.rd(loc, w), cnt, w))
        elif op == 0xC2:
            n = self.fetch16()
            self.ip = self.pop()
            r[SP] = (r[SP] + n) & 0xFFFF
        elif op == 0xC3:
            self.ip = self.pop()
        elif op in (0xC4, 0xC5):
            reg, loc = self.modrm()
            r[reg] = self.rw(loc[1], loc[2])
            self.s[ES if op == 0xC4 else DS] = self.rw(loc[1], loc[2] + 2)
        elif op in (0xC6, 0xC7):
            w = op & 1
            reg, loc = self.modrm()
            self.wr(loc, w, self.fetch16() if w else self.fetch8())
        elif op in (0xCA, 0xCB):
            n = self.fetch16() if op == 0xCA else 0
            ip = self.pop()
            cs = self.pop()
            r[SP] = (r[SP] + n) & 0xFFFF
            self.far_to(cs, ip)
        elif op == 0xCD:
            n = self.fetch8()
            if not (self.int_handler and self.int_handler(self, n)):
                raise EmuError("unhandled int %02Xh at %04X:%04X" % (n, start_cs, start_ip))
        elif op == 0xCF:
            ip = self.pop()
            cs = self.pop()
            self.setflags(self.pop())
            self.far_to(cs, ip)
        elif op == 0xD7:
            seg = self.s[self.seg_ovr if self.seg_ovr is not None else DS]
            self.setr8(0, self.rb(seg, r[BX] + (r[AX] & 0xFF)))
        elif op in (0xE0, 0xE1, 0xE2, 0xE3):
            d = self.fetchs8()
            if op == 0xE3:
                take = r[CX] == 0
            else:
                r[CX] = (r[CX] - 1) & 0xFFFF
                take = r[CX] != 0
                if op == 0xE0:
                    take = take and not self.zf
                elif op == 0xE1:
                    take = take and self.zf
            if take:
                self.ip = (self.ip + d) & 0xFFFF
        elif op in (0xE4, 0xE5, 0xEC, 0xED):
            if op in (0xE4, 0xE5):
                self.fetch8()
            r[AX] = r[AX] if op & 1 == 0 else 0     # port reads: return junk-free 0
        elif op in (0xE6, 0xE7, 0xEE, 0xEF):
            if op in (0xE6, 0xE7):
                self.fetch8()
        elif op == 0xE8:
            d = self.fetch16()
            self.push(self.ip)
            self.ip = (self.ip + d) & 0xFFFF
        elif op == 0xE9:
            d = self.fetch16()
            self.ip = (self.ip + d) & 0xFFFF
        elif op == 0xEA:
            ip = self.fetch16()
            cs = self.fetch16()
            self.far_to(cs, ip)
        elif op == 0xEB:
            d = self.fetchs8()
            self.ip = (self.ip + d) & 0xFFFF
        elif op == 0xF4:
            self.halted = True
        elif op == 0xF5:
            self.cf ^= 1
        elif op in (0xF6, 0xF7):
            w = op & 1
            reg, loc = self.modrm()
            v = self.rd(loc, w)
            if reg in (0, 1):
                imm = self.fetch16() if w else self.fetch8()
                self.alu(4, v, imm, w)
            elif reg == 2:
                self.wr(loc, w, ~v)
            elif reg == 3:
                res = self.alu(5, 0, v, w)
                self.cf = int(v != 0)
                self.wr(loc, w, res)
            elif reg in (4, 5):
                if reg == 4:
                    if w:
                        res = r[AX] * v
                        r[AX], r[DX] = res & 0xFFFF, res >> 16
                        self.cf = self.of = int(r[DX] != 0)
                    else:
                        res = (r[AX] & 0xFF) * v
                        r[AX] = res & 0xFFFF
                        self.cf = self.of = int((res >> 8) != 0)
                else:
                    def sx(x, b):
                        return x - (1 << b) if x & (1 << (b - 1)) else x
                    if w:
                        res = sx(r[AX], 16) * sx(v, 16)
                        r[AX], r[DX] = res & 0xFFFF, (res >> 16) & 0xFFFF
                        self.cf = self.of = int(not (-0x8000 <= res <= 0x7FFF))
                    else:
                        res = sx(r[AX] & 0xFF, 8) * sx(v, 8)
                        r[AX] = res & 0xFFFF
                        self.cf = self.of = int(not (-0x80 <= res <= 0x7F))
            elif reg == 6:
                if v == 0:
                    raise EmuError("divide by zero at %04X:%04X" % (start_cs, start_ip))
                if w:
                    num = (r[DX] << 16) | r[AX]
                    q, rem = divmod(num, v)
                    if q > 0xFFFF:
                        raise EmuError("div overflow")
                    r[AX], r[DX] = q, rem
                else:
                    q, rem = divmod(r[AX], v)
                    if q > 0xFF:
                        raise EmuError("div overflow")
                    r[AX] = q | (rem << 8)
            else:
                raise EmuError("idiv not implemented at %04X:%04X" % (start_cs, start_ip))
        elif op == 0xF8:
            self.cf = 0
        elif op == 0xF9:
            self.cf = 1
        elif op == 0xFA:
            self.ifl = 0
        elif op == 0xFB:
            self.ifl = 1
        elif op == 0xFC:
            self.df = 0
        elif op == 0xFD:
            self.df = 1
        elif op in (0xFE, 0xFF):
            w = op & 1
            reg, loc = self.modrm()
            if reg in (0, 1):
                cf = self.cf
                self.wr(loc, w, self.alu(0 if reg == 0 else 5, self.rd(loc, w), 1, w))
                self.cf = cf
            elif reg == 2:
                t = self.rd(loc, 1)
                self.push(self.ip)
                self.ip = t
            elif reg == 3:
                ip, cs = self.rw(loc[1], loc[2]), self.rw(loc[1], loc[2] + 2)
                self.push(self.s[CS])
                self.push(self.ip)
                self.far_to(cs, ip)
            elif reg == 4:
                self.ip = self.rd(loc, 1)
            elif reg == 5:
                self.far_to(self.rw(loc[1], loc[2] + 2), self.rw(loc[1], loc[2]))
            elif reg == 6:
                self.push(self.rd(loc, 1))
            else:
                raise EmuError("bad FF /7")
        else:
            raise EmuError("unimplemented opcode %02X at %04X:%04X" % (op, start_cs, start_ip))
