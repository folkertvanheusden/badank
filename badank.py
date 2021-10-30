#! /usr/bin/python3

from enum import Enum
import subprocess
import time

class Color(Enum):
    WHITE = 1
    BLACK = 2

class TextProgram:
    def __init__(self, program, dir_ = None):
        if dir_ is None:
            self.proc = subprocess.Popen(program, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        else:
            self.proc = subprocess.Popen(program, stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=dir_)

    def read(self):
        try:
            return self.proc.stdout.readline().decode('utf-8').rstrip('\n')
        
        except Exception as e:
            print('TextProgram: %s' % e)
            return None

    def write(self, what):
        out = '%s\n\n' % what
        self.proc.stdin.write(bytes(out, 'utf-8'))
        self.proc.stdin.flush()

    def stop(self):
        self.write('quit')
        self.proc.stdin.close()
        self.proc.stdout.close()

class GtpEngine:
    def __init__(self, program, dir_ = None):
        self.engine = TextProgram(program, dir_)

    def getresponse(self):
        while True:
            line = self.engine.read()
            if line == '':
                continue

            if line == None:
                return None

            if line[0] == '=':
                return line[2:]

    def genmove(self, color):
        if color == Color.WHITE:
            self.engine.write('genmove w')

        else:
            self.engine.write('genmove b')

        return self.getresponse()

    def boardsize(self, dim):
        self.engine.write('boardsize %d' % dim)

        self.getresponse()

    def clearboard(self):
        self.engine.write('clear_board')

        self.getresponse()

    def play(self, color, vertex):
        self.engine.write('play %s %s' % ('black' if color == Color.BLACK else 'white', vertex))

        self.getresponse()

    def getscore(self):
        self.engine.write('final_score')

        return self.getresponse()

    def getname(self):
        self.engine.write('name')

        return self.getresponse()

    def stop(self):
        self.engine.stop()

class Board:
    def __init__(self, dim):
        self.b = [ [ None ] * dim ] * dim

    def get(self, coordinates):
        return self.b[coordinates[1]][coordinates[0]]

    def set(self, coordinates, what):
        self.b[coordinates[1]][coordinates[0]] = what

class Scorer:
    def __init__(self, program):
        self.p = GtpEngine(program)

    def restart(self, dim):
        self.p.clearboard()
        self.p.boardsize(dim)

    def play(self, color, vertex):
        self.p.play(color, vertex)

    def getscore(self):
        return self.p.getscore()

def gtp_vertex_to_coordinates(s):
    x = ord(s[0]) - ord('a')
    if s[0] >= 'i':
        x -= 1

    y = ord(s[1]) - ord('1')

    return (x, y)

def play(pb, pw, board_size, scorer):
    scorer.restart(board_size)

    b = Board(board_size)

    pb.boardsize(board_size)
    pw.boardsize(board_size)

    whitePass = blackPass = 0

    color = Color.BLACK

    result = None

    while True:
        if color == Color.BLACK:
            move = pb.genmove(color)
            pw.play(color, move)

        else:
            move = pw.genmove(color)
            pb.play(color, move)

        move = move.lower()
        print(color, move)

        scorer.play(color, move)

        if move == 'pass':
            if color == Color.BLACK:
                blackPass += 1
            else:
                whitePass += 1

            if blackPass == 3 or whitePass == 3:
                break

        elif move == 'resign':
            if color == Color.BLACK:
                result = "W"
            else:
                result = "B"

            break

        else:
            coordinates = gtp_vertex_to_coordinates(move)

            b.set(coordinates, color)

        if color == Color.BLACK:
            color = Color.WHITE
        else:
            color = Color.BLACK

    if not result:
        result = scorer.getscore()

    return result

def play_game(p1, p1dir, p2, p2dir, ps, dim, pgn_file):
    scorer = Scorer(ps)

    inst1 = GtpEngine(p1, p1dir)
    name1 = inst1.getname()

    inst2 = GtpEngine(p2, p2dir)
    name2 = inst2.getname()

    print('%s versus %s' % (name1, name2))

    result = play(inst1, inst2, dim, scorer).lower()
    print(result)

    if result[0] == 'b':
        result_pgn = '0-1'
    elif result[0] == 'w':
        result_pgn = '1-0'
    else:
        result_pgn = '1/2-1/2'

    h = open(pgn_file, 'a')
    h.write('[White "%s"]\n[Black "%s"]\n[Result "%s"]\n\n%s\n\n' % (name2, name1, result_pgn, result_pgn))
    h.close()

    inst2.stop()

    inst1.stop()

play_game(['/usr/bin/java', '-jar', '/home/folkert/Projects/stop/trunk/stop.jar', '--mode', 'gtp'], None, ['/home/folkert/Projects/baduck/build/src/donaldbaduck'], None, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn')

play_game(['/usr/bin/java', '-jar', '/home/folkert/Projects/stop/trunk/stop.jar', '--mode', 'gtp'], None, ['/home/folkert/Projects/daffyduck/build/src/daffybaduck'], None, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn')

play_game(['/home/folkert/Projects/baduck/build/src/donaldbaduck'], None, ['/usr/bin/java', '-jar', '/home/folkert/Projects/stop/trunk/stop.jar', '--mode', 'gtp'], None, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn')

play_game(['/home/folkert/Projects/daffyduck/build/src/daffybaduck'], None, ['/usr/bin/java', '-jar', '/home/folkert/Projects/stop/trunk/stop.jar', '--mode', 'gtp'], None, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn')

play_game(['/home/folkert/Projects/baduck/build/src/donaldbaduck'], None, ['/home/folkert/Projects/daffyduck/build/src/daffybaduck'], None, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn')

play_game(['/home/folkert/Projects/daffyduck/build/src/daffybaduck'], None, ['/home/folkert/Projects/baduck/build/src/donaldbaduck'], None, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn')
