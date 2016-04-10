import sys
import socket
from PIL import Image

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 1234))

def pixel(x,y,r,g,b,a=255):
  if a == 255:
    sock.send('PX %d %d %02x%02x%02x\n' % (x,y,r,g,b))
  else:
    sock.send('PX %d %d %02x%02x%02x%02x\n' % (x,y,r,g,b,a))

im = Image.open(sys.argv[1]).convert('RGBA')
_,_,w,h = im.getbbox()
for x in xrange(w):
  for y in xrange(h):
    r,g,b,a = im.getpixel((x,y))
    pixel(x,y,r,g,b,a)

