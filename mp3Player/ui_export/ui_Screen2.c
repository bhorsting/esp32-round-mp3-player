#include "ui.h"
lv_obj_t *ui_Screen2 = NULL;lv_obj_t *ui_Image4 = NULL;
void ui_Screen2_screen_init(void){
ui_Screen2=lv_obj_create(NULL);lv_obj_clear_flag(ui_Screen2,LV_OBJ_FLAG_SCROLLABLE);
ui_Image4=lv_img_create(ui_Screen2);
lv_obj_set_width(ui_Image4,LV_SIZE_CONTENT);lv_obj_set_height(ui_Image4,LV_SIZE_CONTENT);
lv_obj_set_align(ui_Image4,LV_ALIGN_CENTER);
}
void ui_Screen2_screen_destroy(void){if(ui_Screen2)lv_obj_del(ui_Screen2);ui_Screen2=NULL;ui_Image4=NULL;}
