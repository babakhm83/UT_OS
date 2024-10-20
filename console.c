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

#define C(x)  ((x)-'@')  // Control-x

//PAGEBREAK: 50
#define _UP_ARROW 0xe2
#define _DOWN_ARROW 0xe3
#define _LEFT_ARROW 0xe4
#define _RIGHT_ARROW 0xe5

#define PRINTF(...) {release(&cons.lock);cprintf(__VA_ARGS__);acquire(&cons.lock);}
#define True 1
#define False 0
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


#define _MOD(a,b) (a%b+b)%b
#define _N_HISTORY 11
struct _buffer _history[_N_HISTORY];
int _current_history=0;
int _last_history=0;
int _arrow = 0;

#define ___HIGH_BYTE_CUR 14
#define ___LOW_BYTE_CUR 15

int
_get_cursor_pos()
{
  int _pos;
  // Cursor position: col + 80*row.
  outb(CRTPORT, ___HIGH_BYTE_CUR);
  _pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, ___LOW_BYTE_CUR);
  _pos |= inb(CRTPORT+1);
  return _pos;
}

void
_update_cursor(int _pos, char symbol)
{
  outb(CRTPORT, ___HIGH_BYTE_CUR);
  outb(CRTPORT+1, _pos>>8);
  outb(CRTPORT, ___LOW_BYTE_CUR);
  outb(CRTPORT+1, _pos);
  if(symbol!=0)
    crt[_pos] = symbol | 0x0700;
}

void
_write_from_buffer()
{
  for (int i = 0; i < INPUT_BUF; i++)
  {
    if(input.buf[i]=='\n' || input.buf[i]=='\0')
      break;
    consputc((int)input.buf[i]);
  }
  input.w=0;
  input.r=0;
}

void
___clear_cmd(int end)
{
  int _cursor_pos = _get_cursor_pos();
  _update_cursor(_cursor_pos-_arrow,0);
  for (int i = 0; i < end - input.w; i++) {
    consputc(BACKSPACE); 
  }
}

void
_arrow_key_console_handler(int c)
{
  int _pos=_get_cursor_pos();
  if(c == _LEFT_ARROW)
  {
    if(-_arrow < input.e)
    {
      --_pos;
      _arrow--;
      _update_cursor(_pos,0);
    }
  }
  else if(c == _RIGHT_ARROW)
  {
    if(_arrow)
    {
      ++_pos;
      _arrow++;
      _update_cursor(_pos,0);
    }
  }
  else
  {
    if (c == _UP_ARROW)
      if(_history[_MOD(_current_history-1,_N_HISTORY)].buf[0]=='\0' || 
      _MOD(_current_history-1,_N_HISTORY)==_MOD(_last_history,_N_HISTORY))
        return;
    if (c == _DOWN_ARROW)
      if(_MOD(_current_history+1,_N_HISTORY)==_MOD(_last_history+1,_N_HISTORY))
        return;
    input.buf[input.e]='\n';
    ___clear_cmd(input.e);
    _arrow=0;
    input.w=++input.e;
    if (_MOD(_current_history,_N_HISTORY)==_MOD(_last_history,_N_HISTORY))
      _history[_MOD(_current_history,_N_HISTORY)]=input;
    if(c == _UP_ARROW)
      _current_history--;
    else
      _current_history++;
    input=_history[_MOD(_current_history,_N_HISTORY)];
    input.e--;
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


void
___shift_buf(int right_flag, int change_idx){
    if (right_flag)
    {
      for (int i = input.e; i > change_idx; i--)
      {
        input.buf[i % INPUT_BUF] = input.buf[(i-1) % INPUT_BUF];
      }
      ++input.e;
    }
    else
    {
      for (int i = change_idx-1; i < input.e; i++)
      {
        input.buf[i % INPUT_BUF] = input.buf[(i+1) % INPUT_BUF];
      }
      --input.e;
    }
}

#define ___BACKSPACE 8

void ___update_buffer(int c, int change_idx)
{
  if (c <= 0)
    return;
  if (c == BACKSPACE || c == ___BACKSPACE)
  {
    ___shift_buf(0,change_idx);
  }
  else
  {
    ___shift_buf(1,change_idx);
    input.buf[(change_idx)%INPUT_BUF]=c;
  }
}

void
_input_in_mid(int c)
{
  int _cursor_pos=_get_cursor_pos();
  int change_index = input.e + _arrow;
  if(c==BACKSPACE || c == ___BACKSPACE)
  {
    if (_arrow <= -input.e) // no extra backspace allowed
      return;
    ___update_buffer(c,change_index);
    ___clear_cmd(input.e + 1);
    _write_from_buffer();
    _update_cursor(_cursor_pos-1,0);
  }
  else
  {
    ___update_buffer(c,change_index);
    ___clear_cmd(input.e - 1);
    _write_from_buffer();
    _update_cursor(_cursor_pos+1,0);
  }
}

void
_history_command()
{
  release(&cons.lock);
  cprintf("Command history:\n");
  cprintf("-------------------------------------------------------------------------------\n");
  for (int i = 0; i < _N_HISTORY-1; i++) {
    if (_history[_MOD(_current_history-i-1,_N_HISTORY)].buf[0]=='\0')
      break;
    cprintf("*%d: %s", i + 1, _history[_MOD(_current_history-i-1,_N_HISTORY)].buf);
  }
  cprintf("\n$ ");
  acquire(&cons.lock);
}

int
_buffer_with_str_cmp(char* target_str)
{
  int i=0;
  while(target_str[i++]!='\0')
    if (input.buf[i]!=target_str[i])
      return 1;
  return 0;
}

struct __exp_found {
  char num_str[INPUT_BUF];
  int num_float;
  int num_size;
  int start_exp_idx;
  int exp_size;
  int success_flag;
} dum;

int __is_in_arr(char inc, char* arr) {
  char c = 'a';
  int i = 0;
  while (c != '\0')
  {
    c = arr[i];
    if (c == inc)
      return 1;  
    i += 1;
  }
  return 0;
}

int __char_to_int(char* arr, int n) {
  int result = 0;
  for (int i = 0; i <= n; i++)
    result = 10 * result + arr[i] - '0';
  return result;
}

struct __exp_found
__int_to_char(int n, struct __exp_found exp) {
  int t = n;
  int sign =0;
  int l = 0;
  if (n == 0)
  {
    exp.num_size = 1;
    exp.num_str[0] = '0';
    return exp;
  }
  if (n < 0)
  {
    exp.num_str[l] = '-';
    sign++;
    l++;
    t = -t;
    n = -n;
  }

  while (t > 0.9)
  {
    t /= 10;
    l++;
  }
  for (int i = sign; i < l; i++)
  {
    exp.num_str[l-i-(sign?0:1)] = (n % 10) + '0'; // if negative start from one place in right
    n /= 10;
  }
  exp.num_size = l; 
  return exp;
}

struct __exp_found
__float1p_to_char(float n, struct __exp_found exp) {
  float temp = n * 10;
  int t = temp;
  int l = 0;
  int sign = 0;
  if (n < 0)
  {
    exp.num_str[l] = '-';
    sign++;
    l++;
    t = -t;
    temp = -temp;
  }
  while (t > 0.9)
  {
    t /= 10;
    l++;
  }
  t = temp;
  for (int i = sign; i < l; i++)
  {
    exp.num_str[l - i- (sign?0:1)] = (t % 10) + '0';
    t /= 10;
  }

  // handling floating point
  l += 1;
  exp.num_str[l - 1] =  exp.num_str[l-2];
  exp.num_str[l-2] = '.';
  exp.num_size = l;
  return exp;
}

struct __exp_found
__solve_exp(char* txt, int break_index, int end_index)
{
  struct __exp_found exp;
  exp.success_flag = 1;
  exp.exp_size = end_index + 2 + 1; // 2 for =? and 1 for index

  int num1 = __char_to_int(&txt[0], break_index);
  int num2 = __char_to_int(&txt[break_index + 2], end_index - 2 - break_index);
  int result = -1;
  switch (txt[break_index + 1])
  {
  case '+':{
    result = num1 + num2;
    exp.num_float = result;
    exp = __int_to_char(result, exp);
    }break;
  
  case '-':{
    result = num1 - num2;
    exp.num_float = result;
    exp = __int_to_char(result, exp);
    }break;
  
  case '*':{
    result = num1 * num2;
    exp.num_float = result;
    exp = __int_to_char(result, exp);
    }break;
  
  case '/':{
    float num1_float = num1;
    float r = num1_float / num2;
    exp.num_float = r;
    exp = __float1p_to_char(r, exp);
    }break;
  
  default:
    break;
  }

  for (int i = exp.num_size; i < INPUT_BUF; i++)
     exp.num_str[i] = '\0';

  return exp; 
}

struct __exp_found
__find_expression()
{
  struct __exp_found no_exp;
  no_exp.success_flag = 0; 
  char c = 'a';
  char nums[11] = "0123456789";
  char ops[5] = "+-*/";
  int i = 0;
  int s = 0;
  int num1_start, num1_end, num2_end;
  while (c != '\0')
  {
    c = input.buf[i];
    switch (s)
    {
    case 0:{
      if (__is_in_arr(c,nums))
      {
        num1_start = i;
        num1_end = i;
        s = 1;
      }
      }break;

    case 1:{
      if (__is_in_arr(c,nums))
        num1_end = i;
      else if (__is_in_arr(c,ops))
        s = 2;
      else
        s = 0;
      }break;
    
    case 2:{
      if (__is_in_arr(c,nums))
      {
        num2_end = i;
        s = 3;
      }
      else
        s = 0;
      }break;

    case 3:{
      if (__is_in_arr(c,nums))
        num2_end = i;
      else if (c == '=')
        s = 4;
      else
        s = 0;
      }break;
 
    case 4:{
      if (c == '?')
        s = 5;
      else
        s = 0;
      }break;

    case 5:{
      struct __exp_found found_exp = __solve_exp(&input.buf[num1_start], num1_end - num1_start, num2_end - num1_start);
      found_exp.start_exp_idx = num1_start;
      return found_exp;
      }break;

    default:
      s = 0;
      break;
    }
    i += 1;
  }
  return no_exp;
}

void
___clear_buf_with_range(int start, int exp_size){
  for (int i = start; i < start + exp_size; i++)
    input.buf[i] = '\0';
}

void
___put_num_in_buf(char* num, int num_size, int start){
  for (int i = start; i < num_size + start; i++)
    input.buf[i] = num[i - start];
}

void
___shift_buf_many_times(int shift_count, int change_idx){
  for (int i = 0; i < shift_count; i++)
    ___shift_buf(0,change_idx - i);
};

void
_handle_custom_commands()
{
  char _history_str[]="history\n";
  if (!_buffer_with_str_cmp(_history_str)) {
    _history_command();
    input.e = input.w = input.r = 0;
  }
}

void
___handle_ctrl_s(int (*getc)(void))
{
  int ___inputs_idx[INPUT_BUF] ;
  for (int i = 0; i < INPUT_BUF; i++)
    ___inputs_idx[i] = 0;
  
  int c;
  while((c = getc()) != C('F'))
  {
    if (c <= 0 || c == _UP_ARROW || c == _DOWN_ARROW || c == C('S'))
      continue;

    else if (c == ___BACKSPACE && _arrow == 0) 
    {
      input.buf[--input.e] = '\0';
      consputc(BACKSPACE);
      if(___inputs_idx[input.e] == 1)
        ___inputs_idx[input.e] = 0;
    }

    else if (c == _LEFT_ARROW || c == _RIGHT_ARROW)
    {
      _arrow_key_console_handler(c);
    }

    else if(input.e-input.r < INPUT_BUF)
    {
      c = (c == '\r') ? '\n' : c;
      if (_arrow == 0)
      {
        ___inputs_idx[input.e] = 1;
        input.buf[input.e++ % INPUT_BUF] = c;
        consputc(c);
      }
      else
      {
        _input_in_mid(c);
        if (c == BACKSPACE || c == ___BACKSPACE)
        {
          for (int i = input.e + _arrow - 1; i < INPUT_BUF - 1; i++)
            ___inputs_idx[i] = ___inputs_idx[i+1];
        }
        else
        {
          for (int i = INPUT_BUF - 1; i > input.e + _arrow - 1 ; i--)
            ___inputs_idx[i] = ___inputs_idx[i-1];
          ___inputs_idx[input.e + _arrow - 1] = 1;
        }
      }
    } 
    struct __exp_found exp = __find_expression();
    if (exp.success_flag)
    {
      int prev_e = input.e;
      int init_cursor_pos = _get_cursor_pos(), init_arrow = _arrow;
      int line_start = init_cursor_pos - init_arrow -prev_e;
      ___clear_buf_with_range(exp.start_exp_idx,exp.exp_size);
      ___put_num_in_buf(exp.num_str,exp.num_size,exp.start_exp_idx);
      int shift_count = exp.exp_size - exp.num_size;
      int change_idx = exp.start_exp_idx + exp.exp_size ;
      ___shift_buf_many_times(shift_count,change_idx);
      _arrow = exp.start_exp_idx + exp.num_size - input.e;
      _update_cursor( line_start + exp.start_exp_idx +  exp.exp_size,0);// moving curser so clear cmd works
      ___clear_cmd(prev_e);
      _write_from_buffer();
      _update_cursor( line_start + exp.start_exp_idx +  exp.num_size,0);
    }
  }

  release(&cons.lock);

  int current_e = input.e;
  char buf_copy[INPUT_BUF];
  for (int i = 0; i < INPUT_BUF; i++) // hard copy
    buf_copy[i] = input.buf[i];

  int change_idx = current_e + _arrow;

  for(int i = 0; i < INPUT_BUF; i++)
    if (___inputs_idx[i] == 1)
      ___update_buffer(buf_copy[i],change_idx++);
  
  ___clear_cmd(current_e);
  _write_from_buffer();
  acquire(&cons.lock);
}

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c)
    {
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
        if (_arrow==0)
        {
          input.buf[--input.e] = '\0';
          consputc(BACKSPACE);
        }
        else
        {
          _input_in_mid(BACKSPACE);
        }
      }
      break;

    // Handle arrow keys
    case _LEFT_ARROW: 
    case _RIGHT_ARROW:
    case _UP_ARROW: 
    case _DOWN_ARROW:
      _arrow_key_console_handler(c);
      break;
    
    //handle ctrl + s
    case C('S'):
    {
      ___handle_ctrl_s(getc);
      int pos = _get_cursor_pos();
      _update_cursor(pos + _arrow,0);
    }
      break;

    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;

        if(c=='\n')
          _arrow=0;

        if (_arrow==0)
        {
          input.buf[input.e++ % INPUT_BUF] = c;
          consputc(c);
        }
        else
        {
          _input_in_mid(c);
        }
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
          _handle_custom_commands();
        }
      }
      break;
    }
    struct __exp_found exp = __find_expression();
    if (exp.success_flag)
    {
      int prev_e = input.e;
      int init_cursor_pos = _get_cursor_pos(), init_arrow = _arrow;
      int line_start = init_cursor_pos - init_arrow -prev_e;
      ___clear_buf_with_range(exp.start_exp_idx,exp.exp_size);
      ___put_num_in_buf(exp.num_str,exp.num_size,exp.start_exp_idx);
      int shift_count = exp.exp_size - exp.num_size;
      int change_idx = exp.start_exp_idx + exp.exp_size ;
      ___shift_buf_many_times(shift_count,change_idx);
      _arrow = exp.start_exp_idx + exp.num_size - input.e;
      _update_cursor( line_start + exp.start_exp_idx +  exp.exp_size,0);// moving curser so clear cmd works
      ___clear_cmd(prev_e);
      _write_from_buffer();
      _update_cursor( line_start + exp.start_exp_idx +  exp.num_size,0);
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

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
      _history[_MOD(_last_history++,_N_HISTORY)]=input;
      _current_history=_last_history;
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