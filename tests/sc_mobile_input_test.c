#include "sc_mobile_input.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    ScTouchInput s={0};s.visible=true;ScTouchLayout(&s,900,450);
    ScTouchRect b=s.buttons[4];ScTouchUpdate(&s,1,b.x+5,b.y+5,true,true);
    assert(s.pad==1&&!s.mouse_buttons);
    ScTouchUpdate(&s,1,450,200,true,false);assert(s.pad==1&&!s.mouse_buttons);
    ScTouchUpdate(&s,2,400,220,true,true);assert(s.pad==1&&s.mouse_buttons==1&&s.mouse_x==400);
    ScTouchUpdate(&s,3,500,220,true,true);assert(s.mouse_buttons==2&&s.mouse_x==450);
    ScTouchUpdate(&s,1,450,200,false,false);assert(!s.pad&&s.mouse_buttons==2);
    ScTouchClear(&s);assert(!ScTouchActive(&s)&&!s.mouse_buttons&&!s.pad);
    b=s.buttons[17];ScTouchUpdate(&s,4,b.x+5,b.y+5,true,true);assert(!s.visible&&!s.mouse_buttons);
    ScTouchClear(&s);ScTouchUpdate(&s,5,300,200,true,true);assert(s.mouse_buttons==1);
    ScTouchUpdate(&s,5,310,205,true,false);assert(s.mouse_x==310&&s.mouse_y==205);
    ScTouchUpdate(&s,5,310,205,false,false);assert(!ScTouchActive(&s)&&!s.mouse_buttons);
    ScTouchLayout(&s,1200,600);assert(s.buttons[17].x+s.buttons[17].w<1200);
    s.visible=true;b=s.buttons[16];ScTouchUpdate(&s,6,b.x+5,b.y+5,true,true);assert(s.pan&&!s.mouse_buttons);
    ScTouchUpdate(&s,6,b.x+5,b.y+5,false,false);ScTouchUpdate(&s,7,400,300,true,true);assert(s.mouse_buttons==2);
    ScTouchClear(&s);
    unsigned char *invalid=calloc(1,0x80201);unsigned offset=999;
    assert(!ScMobileRomValid(invalid,0x80201,&offset));assert(!ScMobileRomValid(invalid,0x80000,&offset));assert(offset==999);free(invalid);
    puts("Mobile control ownership, multitouch, release, resize and ROM rejection passed");
}
