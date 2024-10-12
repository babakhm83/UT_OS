// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define _UP_ARROW 0xe2
#define _DOWN_ARROW 0xe3
#define _LEFT_ARROW 0xe4
#define _RIGHT_ARROW 0xe5
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

#define INPUT_BUF 128
struct _buffer {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} input;

struct _buffer _history[10];
int _current_history=0;

int
_get_cursor_pos()
{
  int _pos;
  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  _pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  _pos |= inb(CRTPORT+1);
  return _pos;
}

void
_update_cursor(int _pos, char _symbol)
{
  outb(CRTPORT, 14);
  outb(CRTPORT+1, _pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, _pos);
  crt[_pos] = _symbol | 0x0700;
}

void
_write_from_buffer()
{
  for (int i = 0; i < INPUT_BUF; i++)
  {
    if(input.buf[i]=='\n')
      break;
    consputc((int)input.buf[i]);
  }
  input.e--;
  input.w=0;
  input.r=0;
}

void
_arrow_key_console_handler(int c)
{
  int _pos=_get_cursor_pos();
  if(c == _LEFT_ARROW)
  {
    if(_pos%80 > 2) --_pos;
    _update_cursor(_pos,crt[_pos]);
  }
  else if(c == _RIGHT_ARROW)
  {
    if(_pos%80<=input.e%80+1) ++_pos;
    _update_cursor(_pos,crt[_pos]);
  }
  else
  {
    input.buf[input.e]='\n';
    for (int i = 0; i < input.e - input.w; i++) {
      consputc(BACKSPACE); 
    }
    input.w=++input.e;
    _history[(_current_history%10+10)%10]=input;
    if(c == _UP_ARROW)
      _current_history++;
    else
      _current_history--;
    input=_history[(_current_history%10+10)%10];
    _write_from_buffer();
  }
}

static void
cgaputc(int c)
{
  int pos=_get_cursor_pos();
  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }
  _update_cursor(pos,' ');
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c);
}

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'):  // Kill line.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.e != input.w){
        input.e--;
        consputc(BACKSPACE);
      }
      break;

    // Handle arrow keys
    case _LEFT_ARROW: 
    case _RIGHT_ARROW:
    case _UP_ARROW: 
    case _DOWN_ARROW:
      _arrow_key_console_handler(c);
      break;

    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;
        consputc(c);
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

void
_print_code_of_char()
{
  for(int i = 0; i < INPUT_BUF; i++) {
    cprintf("%x ",(int)input.buf[i]);
  }  
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;
  // _print_code_of_char(); //Used this function to find code for left and right key arrow

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.r == input.w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
    {
      _history[(_current_history++%10+10)%10]=input;
      struct _buffer new_input={"",0,0,0};
      input=new_input;
      break;
    }
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

