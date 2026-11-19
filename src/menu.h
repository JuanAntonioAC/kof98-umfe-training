#ifndef KOF98_TRAIN_MENU_H
#define KOF98_TRAIN_MENU_H

void menu_init(void* hwnd_parent_unused);
void menu_pump(void);
void menu_toggle(void);
void menu_request_show(void);
int menu_open(void);
void menu_on_word(unsigned word);
void menu_note(const char* text);

#endif
