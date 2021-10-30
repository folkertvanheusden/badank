#! /usr/bin/python3

from enum import Enum
import logging
import logging.handlers
import psutil
from queue import Queue
from select import select
import subprocess
import sys
from threading import Lock, Thread
import time

### logging
logger = logging.getLogger('badank')
logger.setLevel(logging.DEBUG)

# file
handler = logging.FileHandler("badank.log")
handler.setLevel(logging.DEBUG)

formatter = logging.Formatter("%(asctime)s [%(levelname)s]: %(message)s")
handler.setFormatter(formatter)

logger.addHandler(handler)

# console
handler = logging.StreamHandler(sys.stdout)
handler.setLevel(logging.INFO)

formatter = logging.Formatter("%(asctime)s: %(message)s")
handler.setFormatter(formatter)

logger.addHandler(handler)
###

pgn_file_lock = Lock()

class Color(Enum):
    WHITE = 1
    BLACK = 2

class TextProgram:
    def __init__(self, program, dir_ = None):
        self.name = program

        if dir_ is None:
            self.proc = subprocess.Popen(program, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        else:
            self.proc = subprocess.Popen(program, stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=dir_)

    def read(self, timeout = None):
        try:
            if timeout != None:
                poll_result = select([self.proc.stdout], [], [], timeout)[0]
                if len(poll_result) == 0:
                    logger.warning('Program %s did not respond in time (%fs) (%s)' % (self.name[0], timeout, self.name))
                    return None

            return self.proc.stdout.readline().decode('utf-8').rstrip('\n')
        
        except Exception as e:
            logger.warning('Program %s crashed? (%s)' % (self.name[0], self.name))

            return None

    def write(self, what):
        out = '%s\n\n' % what
        self.proc.stdin.write(bytes(out, 'utf-8'))
        self.proc.stdin.flush()

    def stop(self):
        self.write('quit')
        self.proc.stdin.close()
        self.proc.stdout.close()

        try:
            self.proc.wait(timeout=1.0)
        except TimeoutExpired as te:
            logger.warning('Forcibly terminating %s (%s)' % (self.name[0], self.name))
            self.proc.terminate()

            try:
                self.proc.wait(timeout=1.0)
            except TimeoutExpired as te:
                logger.warning('Forcibly killing %s (%s)' % (self.name[0], self.name))
                self.proc.kill()

class GtpEngine:
    def __init__(self, program, dir_ = None):
        self.name = program[0]
        self.engine = TextProgram(program, dir_)

    def getresponse(self):
        while True:
            line = self.engine.read()
            if line == '':
                continue

            if line == None:
                logger.warning('Program %s did not respond' % self.name)
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

        self.name = self.getresponse()
        return self.name

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

    def stop(self):
        self.p.stop()

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
            if move == None:
                result = "W"
                break
            pw.play(color, move)

        else:
            move = pw.genmove(color)
            if move == None:
                result = "B"
                break
            pb.play(color, move)

        move = move.lower()
        #print(color, move)

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

def play_game(meta_str, p1, p2, ps, dim, pgn_file):
    scorer = Scorer(ps)

    inst1 = GtpEngine(p1[0], p1[1])
    name1 = inst1.getname()

    inst2 = GtpEngine(p2[0], p2[1])
    name2 = inst2.getname()

    logger.info('%s%s versus %s started' % (meta_str, name1, name2))

    result = play(inst1, inst2, dim, scorer).lower()
    # print(result)

    if result[0] == 'b':
        result_pgn = '0-1'
    elif result[0] == 'w':
        result_pgn = '1-0'
    else:
        result_pgn = '1/2-1/2'

    pgn_file_lock.acquire()
    h = open(pgn_file, 'a')
    h.write('[White "%s"]\n[Black "%s"]\n[Result "%s"]\n\n%s\n\n' % (name2 if p2[2] is None else p2[2], name1 if p1[2] is None else p1[2], result_pgn, result_pgn))
    h.close()
    pgn_file_lock.release()

    inst2.stop()

    inst1.stop()

    scorer.stop()

    logger.info('%s (black) versus %s (white) result: %s' % (name1, name2, result))

def process_entry(q, scorer, dim, pgn_file):
    while True:
        entry = q.get()
        if entry == None:
            break

        try:
            play_game('%s] ' % entry[2], entry[0], entry[1], scorer, dim, pgn_file)
        except Exception as e:
            logger.error('play_game threw an exception: %s' % e)

def play_batch(engines, scorer, dim, pgn_file, concurrency, iterations):
    logger.info('Batch starting')

    n = len(engines)

    logger.info('Will play %d games' % (n * (n - 1) * iterations))

    q = Queue(maxsize = concurrency * 2)

    threads = []

    for i in range(0, concurrency):
        th = Thread(target=process_entry, args=(q, scorer, dim, pgn_file, ))
        th.start()

        threads.append(th)

    nr = 0

    for i in range(0, iterations):
        for a in range(0, n):
            for b in range(0, n):
                if a == b:
                    continue

                q.put((engines[a], engines[b], nr))
                
                nr += 1

    for i in range(0, concurrency):
        q.put(None)

    logger.info('Waiting for threads to finish...')

    for th in threads:
        th.join()

    logger.info('Batch finished')

engines = []
engines.append((['/usr/bin/java', '-jar', '/home/folkert/Projects/stop/trunk/stop.jar', '--mode', 'gtp'], None, None))

engines.append((['/home/folkert/Projects/baduck/build/src/donaldbaduck'], None, None))

engines.append((['/home/folkert/Projects/daffyduck/build/src/daffybaduck'], None, None))

engines.append((['/usr/games/gnugo', '--mode', 'gtp', '--level', '0'], None, 'GnuGO level 0'))

start_cpu_time = psutil.cpu_times()
start_cpu_time_ts = start_cpu_time.user + start_cpu_time.system

start_ts = time.time()

iterations = 100
concurrency = 16
play_batch(engines, ['/usr/games/gnugo', '--mode', 'gtp'], 9, 'test.pgn', concurrency, iterations)

end_cpu_time = psutil.cpu_times()
end_cpu_time_ts = end_cpu_time.user + end_cpu_time.system

end_ts = time.time()

diff_ts = end_ts - start_ts
diff_cpu_time_ts = end_cpu_time_ts - start_cpu_time_ts

logger.info('Took %fs, cpu usage: %fs (%f)' % (diff_ts, diff_cpu_time_ts, diff_cpu_time_ts / diff_ts))
