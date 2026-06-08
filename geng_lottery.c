/*
 * geng_lottery.c
 * Geng-Lottery Analyzer
 * แอปพลิเคชัน GUI วิเคราะห์หวย 3 ตัวบน ใช้ GTK+3 และ libcurl
 * เขียนทั้งหมดในไฟล์เดียวตามข้อกำหนด
 */

#include <gtk/gtk.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

/* ============================================================
 * โครงสร้างและตัวแปรกลาง (Global State)
 * ============================================================ */
// วิดเจ็ต GUI (เปลี่ยน status_label เป็น GtkLabel* เพื่อแก้ warning)
GtkLabel *status_label;          // ใช้ GtkLabel* โดยตรง
GtkWidget *entry_manual;
GtkWidget *spin_button;
GtkWidget *radio_manual, *radio_auto;
GtkWidget *text_view;
GtkTextBuffer *text_buffer;
GtkWidget *profit_label;         // ใช้ GtkWidget* แต่ใช้ GTK_LABEL() ตอนเรียกฟังก์ชัน

// ข้อมูลสถิติหวย 20 งวดล่าสุด
char last20[20][4];             // เก็บเป็นสตริง 3 หลัก
int have_data = 0;              // 1 = โหลดข้อมูลสำเร็จ
int data_mode = 0;              // 0=online, 1=offline, 2=backup

// ผลการคำนวณล่าสุด
char *selected_digits = NULL;   // สตริงเลขเดี่ยวที่ใช้ (เช่น "012")
int total_sets = 0;             // จำนวนชุดเลข 3 ตัว
double investment = 0;          // เงินลงทุน (บาท)
double profit = 0;              // กำไรสุทธิ
int calc_done = 0;              // 1 = เคยกดวิเคราะห์แล้ว
GString *permutation_str = NULL; // ข้อความชุดเลขเรียงสวย ใช้ทำรายงาน

/* ============================================================
 * ฟังก์ชันช่วย (Helpers)
 * ============================================================ */

// แสดงไดอะล็อกข้อความ
void show_message_dialog(GtkWindow *parent, const char *msg, GtkMessageType type) {
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                            type, GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// สกัดเลขโดดเดี่ยวจากข้อความที่ผู้ใช้ป้อน (เอาเฉพาะ 0-9, ตัดซ้ำ, เรียงลำดับ)
gchar* extract_unique_digits(const char *input) {
    int seen[10] = {0};
    const char *p = input;
    while (*p) {
        if (isdigit((unsigned char)*p))
            seen[*p - '0'] = 1;
        p++;
    }
    GString *digits = g_string_new(NULL);
    for (int d = 0; d < 10; d++)
        if (seen[d])
            g_string_append_printf(digits, "%d", d);
    if (digits->len == 0) {
        g_string_free(digits, TRUE);
        return NULL;
    }
    return g_string_free(digits, FALSE); // โอนกรรมสิทธิ์สตริง
}

// หาเลขที่ออก "น้อยที่สุด" จาก 20 งวด (นับความถี่ทุกตำแหน่ง)
void compute_rare_digits(int count, char *digits_out) {
    int freq[10] = {0};
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 3; j++) {
            int d = last20[i][j] - '0';
            freq[d]++;
        }
    }
    // สร้างอาร์เรย์ 0-9 แล้วเรียงตามความถี่น้อยไปมาก (ถ้าเท่ากันเรียงตามค่าเลข)
    int digits[10];
    for (int i = 0; i < 10; i++) digits[i] = i;
    for (int i = 0; i < 9; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (freq[digits[i]] > freq[digits[j]] ||
                (freq[digits[i]] == freq[digits[j]] && digits[i] > digits[j])) {
                int tmp = digits[i];
                digits[i] = digits[j];
                digits[j] = tmp;
            }
        }
    }
    for (int i = 0; i < count; i++)
        digits_out[i] = '0' + digits[i];
    digits_out[count] = '\0';
}

/* ============================================================
 * การดาวน์โหลดและพาร์ส JSON (libcurl)
 * ============================================================ */

// โครงสร้างเก็บข้อมูลดาวน์โหลดในหน่วยความจำ
struct MemoryStruct {
    char *memory;
    size_t size;
};

// callback ของ libcurl สำหรับเขียนข้อมูล
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0; // หน่วยความจำไม่พอ
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

// พาร์สสตริง JSON (อาร์เรย์ของสตริง 3 หลัก) เอาเฉพาะ 20 ตัวล่าสุด
void parse_json_string(const char *json, char out[][4], int *count) {
    char tokens[200][4]; // เก็บชั่วคราวสูงสุด 200 ชุด
    int total = 0;
    const char *p = json;
    while (*p) {
        p = strchr(p, '"');
        if (!p) break;
        p++; // ข้ามเครื่องหมายคำพูดเปิด
        if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && p[3] == '"') {
            if (total < 200) {
                strncpy(tokens[total], p, 3);
                tokens[total][3] = '\0';
                total++;
            }
            p += 4; // ข้ามเลข 3 ตัว + เครื่องหมายคำพูดปิด
        } else {
            p++;
        }
    }
    int copy_count = (total >= 20) ? 20 : total;
    int start = total - copy_count;
    for (int i = 0; i < copy_count; i++)
        strcpy(out[i], tokens[start + i]);
    *count = copy_count;
}

// ดำเนินการโหลดข้อมูลเมื่อเริ่มโปรแกรม (Online → Offline → System Backup)
void download_and_process() {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk = {NULL, 0};
    chunk.memory = malloc(1);
    chunk.size = 0;
    int online_success = 0;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL,
            "https://gist.githubusercontent.com/GTZX26/b702b338ff722881acd93e21b1a04d5e/raw/lottery_stats.json");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "GengLotteryAnalyzer/1.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                int cnt;
                parse_json_string(chunk.memory, last20, &cnt);
                if (cnt >= 20) {
                    have_data = 1;
                    data_mode = 0; // online
                    g_file_set_contents("lottery_stats.json", chunk.memory, -1, NULL);
                    online_success = 1;
                }
            }
        }
        curl_easy_cleanup(curl);
    }
    free(chunk.memory); // คืนหน่วยความจำของ buffer ดาวน์โหลด

    if (!online_success) {
        // ลองอ่านจากไฟล์ในเครื่อง
        gchar *file_content = NULL;
        if (g_file_get_contents("lottery_stats.json", &file_content, NULL, NULL)) {
            int cnt;
            parse_json_string(file_content, last20, &cnt);
            g_free(file_content);
            if (cnt >= 20) {
                have_data = 1;
                data_mode = 1; // offline
                return;
            }
        }
        // ใช้ข้อมูลสำรองฮาร์ดโค้ด
        const char *backup[20] = {
            "176","984","186","435","729","851","362","507","943","618",
            "274","390","165","804","532","697","218","473","925","046"
        };
        for (int i = 0; i < 20; i++)
            strcpy(last20[i], backup[i]);
        have_data = 1;
        data_mode = 2; // system backup

        // สร้างไฟล์ lottery_stats.json ใหม่
        GString *json = g_string_new("[");
        for (int i = 0; i < 20; i++) {
            g_string_append_printf(json, "\"%s\"", backup[i]);
            if (i < 19) g_string_append(json, ",");
        }
        g_string_append(json, "]");
        g_file_set_contents("lottery_stats.json", json->str, -1, NULL);
        g_string_free(json, TRUE);
    }
}

/* ============================================================
 * Callbacks ของ GUI
 * ============================================================ */

// เมื่อเลือกโหมด Manual
void on_manual_toggled(GtkToggleButton *btn, gpointer user_data) {
    if (gtk_toggle_button_get_active(btn)) {
        gtk_widget_set_sensitive(entry_manual, TRUE);
        gtk_widget_set_sensitive(spin_button, FALSE);
    }
}

// เมื่อเลือกโหมด Auto
void on_auto_toggled(GtkToggleButton *btn, gpointer user_data) {
    if (gtk_toggle_button_get_active(btn)) {
        gtk_widget_set_sensitive(entry_manual, FALSE);
        gtk_widget_set_sensitive(spin_button, TRUE);
    }
}

// ปุ่ม "เริ่มวิเคราะห์"
void on_analyze_clicked(GtkButton *btn, gpointer user_data) {
    if (!have_data) {
        gtk_text_buffer_set_text(text_buffer, "❌ ยังไม่มีข้อมูลสถิติหวย 20 งวด", -1);
        return;
    }

    char unique[11] = {0}; // เก็บเลขเดี่ยวที่ใช้ (สูงสุด 10 ตัว)
    int digit_count = 0;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_manual))) {
        // โหมดป้อนเอง
        const char *input = gtk_entry_get_text(GTK_ENTRY(entry_manual));
        char *res = extract_unique_digits(input);
        if (res) {
            strcpy(unique, res);
            digit_count = strlen(unique);
            g_free(res);
        } else {
            digit_count = 0;
        }
    } else {
        // โหมดอัตโนมัติ
        int count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button));
        compute_rare_digits(count, unique);
        digit_count = count;
    }

    if (digit_count < 3) {
        gtk_text_buffer_set_text(text_buffer, "❌ ต้องมีเลขอย่างน้อย 3 ตัวขึ้นไปในการคำนวณ", -1);
        return;
    }

    // เก็บเลขเดี่ยวสำหรับรายงาน
    if (selected_digits) g_free(selected_digits);
    selected_digits = g_strdup(unique);

    // สร้าง Permutation เลข 3 ตัว ไม่ซ้ำตำแหน่ง
    GString *result = g_string_new("");
    int n = digit_count;
    int line_count = 0;
    total_sets = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            for (int k = 0; k < n; k++) {
                if (j == k || i == k) continue;
                char set_str[4];
                set_str[0] = unique[i];
                set_str[1] = unique[j];
                set_str[2] = unique[k];
                set_str[3] = '\0';
                g_string_append(result, set_str);
                total_sets++;
                line_count++;
                if (line_count % 10 == 0)
                    g_string_append(result, "\n");
                else
                    g_string_append(result, " ");
            }
        }
    }

    gtk_text_buffer_set_text(text_buffer, result->str, -1);

    // เก็บสำหรับรายงาน
    if (permutation_str) g_string_free(permutation_str, TRUE);
    permutation_str = result; // โอนกรรมสิทธิ์

    // คำนวณเงินลงทุน กำไร
    investment = total_sets * 1.0;
    profit = 900.0 - investment;
    char profit_text[256];
    snprintf(profit_text, sizeof(profit_text),
             "💵 จำนวนชุด: %d ชุด | เงินลงทุน: %.0f บาท | เงินรางวัล: 900 บาท | กำไรสุทธิ: %.0f บาท",
             total_sets, investment, profit);
    gtk_label_set_text(GTK_LABEL(profit_label), profit_text);
    calc_done = 1;
}

// ปุ่ม "ออกรายงาน"
void on_report_clicked(GtkButton *btn, gpointer user_data) {
    if (!calc_done) {
        show_message_dialog(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn))),
                            "❌ ยังไม่มีการคำนวณผลลัพธ์\nกรุณากด 'เริ่มวิเคราะห์' ก่อน",
                            GTK_MESSAGE_WARNING);
        return;
    }

    // สร้างเนื้อหารายงาน
    GString *report = g_string_new(NULL);
    g_string_append(report, "========================================\n");
    g_string_append(report, "   Geng-Lottery Analyzer - รายงานผล\n");
    g_string_append(report, "========================================\n\n");

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char datetime[64];
    strftime(datetime, sizeof(datetime), "%d/%m/%Y %H:%M:%S", tm_info);
    g_string_append_printf(report, "วันที่/เวลา: %s\n\n", datetime);

    g_string_append_printf(report, "สรุปผลกำไร:\n");
    g_string_append_printf(report, "  - จำนวนชุดเลข 3 ตัว: %d ชุด\n", total_sets);
    g_string_append_printf(report, "  - เงินลงทุน: %.0f บาท (ชุดละ 1 บาท)\n", investment);
    g_string_append_printf(report, "  - เงินรางวัลคงที่: 900 บาท\n");
    g_string_append_printf(report, "  - กำไรสุทธิ: %.0f บาท\n\n", profit);

    g_string_append(report, "เลขย้อนหลัง 20 งวดล่าสุดที่ใช้คำนวณ:\n");
    for (int i = 0; i < 20; i++) {
        g_string_append_printf(report, "  %s", last20[i]);
        if ((i + 1) % 5 == 0)
            g_string_append(report, "\n");
        else
            g_string_append(report, " ");
    }
    g_string_append(report, "\n\n");

    g_string_append_printf(report, "เลขที่นำมาเรียง (หลัก): %s\n\n", selected_digits);
    g_string_append(report, "รายการชุดเลข 3 ตัวทั้งหมด:\n");
    g_string_append(report, permutation_str->str);
    g_string_append(report, "\n");

    // เขียนไฟล์
    const char *filename = "geng_lottery_report.txt";
    GError *error = NULL;
    if (!g_file_set_contents(filename, report->str, -1, &error)) {
        show_message_dialog(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn))),
                            "❌ ไม่สามารถบันทึกไฟล์รายงานได้", GTK_MESSAGE_ERROR);
    } else {
        char *cwd = g_get_current_dir();
        char *fullpath = g_build_filename(cwd, filename, NULL);
        char msg[512];
        snprintf(msg, sizeof(msg), "✅ บันทึกไฟล์รายงานเรียบร้อยแล้วที่:\n%s", fullpath);
        show_message_dialog(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn))),
                            msg, GTK_MESSAGE_INFO);
        g_free(cwd);
        g_free(fullpath);
    }
    g_string_free(report, TRUE);
}

/* ============================================================
 * สร้างอินเทอร์เฟซ GUI
 * ============================================================ */
void build_ui() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Geng-Lottery Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(window), 550, 680);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    // ตั้งไอคอนหน้าต่าง
    GError *err = NULL;
    GdkPixbuf *icon = gdk_pixbuf_new_from_file("/home/geng/Desktop/Geng Project/icon-lottery.png", &err);
    if (icon) {
        gtk_window_set_icon(GTK_WINDOW(window), icon);
        g_object_unref(icon);
    } else {
        g_warning("ไม่สามารถโหลดไอคอน: %s", err->message);
        g_clear_error(&err);
    }

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_vbox);

    // ----- ป้ายสถานะเครือข่าย -----
    // ใช้ GtkLabel* โดยตรง สร้างจาก gtk_label_new แล้ว cast เก็บใน status_label
    status_label = GTK_LABEL(gtk_label_new(NULL));  // เก็บเป็น GtkLabel*
    if (data_mode == 0)
        gtk_label_set_markup(status_label, "<span foreground='green'>🟢 Online: อัปเดตสถิติล่าสุดจากเว็บ และบันทึกไฟล์ลงเครื่องสำเร็จ</span>");
    else if (data_mode == 1)
        gtk_label_set_markup(status_label, "<span foreground='orange'>🟡 Offline Mode: อ่านข้อมูลสถิติจากไฟล์ในเครื่องคอมพิวเตอร์เรียบร้อย</span>");
    else
        gtk_label_set_markup(status_label, "<span foreground='red'>🔴 System Backup: ไม่สามารถติดต่อเว็บ/ไฟล์ในเครื่องได้ (ใช้ฐานข้อมูลสำรอง)</span>");
    gtk_box_pack_start(GTK_BOX(main_vbox), GTK_WIDGET(status_label), FALSE, FALSE, 0);

    // ----- Frame เลือกวิธีการทำงาน -----
    GtkWidget *frame1 = gtk_frame_new("เลือกวิธีการทำงาน");
    gtk_box_pack_start(GTK_BOX(main_vbox), frame1, FALSE, FALSE, 0);
    GtkWidget *frame1_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(frame1), frame1_vbox);

    radio_manual = gtk_radio_button_new_with_label(NULL, "โหมดป้อนเอง (Manual)");
    radio_auto = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_manual), "โหมดอัตโนมัติ (Auto)");
    entry_manual = gtk_entry_new();
    spin_button = gtk_spin_button_new_with_range(3, 10, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button), 3);

    gtk_box_pack_start(GTK_BOX(frame1_vbox), radio_manual, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame1_vbox), entry_manual, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame1_vbox), radio_auto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(frame1_vbox), spin_button, FALSE, FALSE, 0);

    // ตั้งค่าเริ่มต้น: Manual
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_manual), TRUE);
    gtk_widget_set_sensitive(entry_manual, TRUE);
    gtk_widget_set_sensitive(spin_button, FALSE);

    g_signal_connect(radio_manual, "toggled", G_CALLBACK(on_manual_toggled), NULL);
    g_signal_connect(radio_auto, "toggled", G_CALLBACK(on_auto_toggled), NULL);

    // ----- ปุ่มควบคุม -----
    GtkWidget *hbox_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), hbox_btns, FALSE, FALSE, 0);
    GtkWidget *btn_analyze = gtk_button_new_with_label("🚀 เริ่มวิเคราะห์แปรผลเลข 3 ตัวบน");
    GtkWidget *btn_report = gtk_button_new_with_label("📋 ออกรายงาน (Report Text)");
    gtk_box_pack_start(GTK_BOX(hbox_btns), btn_analyze, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox_btns), btn_report, TRUE, TRUE, 0);
    g_signal_connect(btn_analyze, "clicked", G_CALLBACK(on_analyze_clicked), NULL);
    g_signal_connect(btn_report, "clicked", G_CALLBACK(on_report_clicked), NULL);

    // ----- Frame รายการชุดตัวเลข -----
    GtkWidget *frame2 = gtk_frame_new("รายการชุดตัวเลขที่ระบบวิเคราะห์ได้");
    gtk_box_pack_start(GTK_BOX(main_vbox), frame2, TRUE, TRUE, 0);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(frame2), scrolled);
    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), text_view);
    text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    // ----- Frame วิเคราะห์เงิน -----
    GtkWidget *frame3 = gtk_frame_new("แผงวิเคราะห์สัดส่วนการลงทุนและการทำกำไร");
    gtk_box_pack_start(GTK_BOX(main_vbox), frame3, FALSE, FALSE, 0);
    profit_label = gtk_label_new("ยังไม่ได้คำนวณ");  // เก็บเป็น GtkWidget*
    gtk_container_add(GTK_CONTAINER(frame3), profit_label);

    gtk_widget_show_all(window);
}

/* ============================================================
 * ฟังก์ชัน main
 * ============================================================ */
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    curl_global_init(CURL_GLOBAL_ALL);

    // โหลดข้อมูลสถิติ (Online / Offline / Backup)
    download_and_process();

    // สร้าง GUI หลังจากข้อมูลพร้อม
    build_ui();

    gtk_main();

    // คืนทรัพยากร
    if (selected_digits) g_free(selected_digits);
    if (permutation_str) g_string_free(permutation_str, TRUE);
    curl_global_cleanup();
    return 0;
}
