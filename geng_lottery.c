#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <curl/curl.h>

// สำหรับแก้ไข Json เมื่อผลหวยออกมาใหม่
// https://gist.github.com/GTZX26/b702b338ff722881acd93e21b1a04d5e

// สำหรับที่อยู่ไฟล์ Joson เอาตรงนี้ไปใส่ใน URL ของโค้ด
#define URL_DB_PAHT "https://gist.github.com/GTZX26/b702b338ff722881acd93e21b1a04d5e/raw"
// กำหนดที่อยู่พาธสำหรับเซฟฐานข้อมูลหวยในเครื่องของพี่เก่ง
#define LOCAL_DB_PATH "lottery_stats.json"

typedef struct {
    int digit;
    int count;
} DigitFreq;

GtkWidget *radio_manual;
GtkWidget *radio_auto;
GtkWidget *entry_manual_digits;
GtkWidget *spin_auto_count;
GtkWidget *text_view_result;
GtkWidget *label_summary;
GtkWidget *window;

char historical_draws[20][4] = {
    "000", "000", "000", "000", "000",
    "000", "000", "000", "000", "000",
    "000", "000", "000", "000", "000",
    "000", "000", "000", "000", "000"
};

size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    size_t realsize = size * nmemb;
    GString *response = (GString *)stream;
    g_string_append_len(response, (const gchar *)ptr, realsize);
    return realsize;
}

bool parse_lottery_json(const char *json_string) {
    int count = 0;
    const char *ptr = json_string;

    while ((ptr = strchr(ptr, '"')) != NULL && count < 20) {
        ptr++;
        if (isdigit(ptr[0]) && isdigit(ptr[1]) && isdigit(ptr[2]) && ptr[3] == '"') {
            strncpy(historical_draws[count], ptr, 3);
            historical_draws[count][3] = '\0';
            count++;
            ptr += 4;
        }
    }
    return (count >= 20);
}

bool read_local_database() {
    FILE *file = fopen(LOCAL_DB_PATH, "r");
    if (!file) return false;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (buffer) {
        size_t read_size = fread(buffer, 1, length, file);
        buffer[read_size] = '\0';
        fclose(file);

        bool success = parse_lottery_json(buffer);
        free(buffer);
        return success;
    }
    fclose(file);
    return false;
}

bool fetch_and_save_live_data() {
    CURL *curl;
    CURLcode res;
    bool success = false;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        GString *chunk = g_string_new("");

        curl_easy_setopt(curl, CURLOPT_URL, URL_DB_PAHT);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Linux Mint Cinnamon) GengLotteryAnalyzer/1.3");

        res = curl_easy_perform(curl);

        if(res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

            if (response_code == 200) {
                success = parse_lottery_json(chunk->str);
                if (success) {
                    FILE *file = fopen(LOCAL_DB_PATH, "w");
                    if (file) {
                        fprintf(file, "%s", chunk->str);
                        fclose(file);
                    }
                }
            }
        }

        g_string_free(chunk, TRUE);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    return success;
}

int compare_freq(const void *a, const void *b) {
    return ((DigitFreq*)a)->count - ((DigitFreq*)b)->count;
}

void analyze_least_frequent(int count, char *output_digits) {
    DigitFreq frequencies[10];
    for(int i = 0; i < 10; i++) {
        frequencies[i].digit = i;
        frequencies[i].count = 0;
    }
    for(int i = 0; i < 20; i++) {
        for(int j = 0; j < 3; j++) {
            int digit = historical_draws[i][j] - '0';
            if(digit >= 0 && digit <= 9) {
                frequencies[digit].count++;
            }
        }
    }
    qsort(frequencies, 10, sizeof(DigitFreq), compare_freq);
    for(int i = 0; i < count; i++) {
        output_digits[i] = frequencies[i].digit + '0';
    }
    output_digits[count] = '\0';
}

void get_unique_digits(const char *input, char *output) {
    bool seen[10] = {false};
    int idx = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        if (isdigit(input[i])) {
            int d = input[i] - '0';
            if (!seen[d]) {
                seen[d] = true;
                output[idx++] = input[i];
            }
        }
    }
    output[idx] = '\0';
}

void on_calculate_clicked(GtkWidget *widget, gpointer data) {
    char digits[11] = "";
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_auto))) {
        int count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_auto_count));
        analyze_least_frequent(count, digits);
        gtk_entry_set_text(GTK_ENTRY(entry_manual_digits), digits);
    } else {
        const char *raw_input = gtk_entry_get_text(GTK_ENTRY(entry_manual_digits));
        get_unique_digits(raw_input, digits);
        gtk_entry_set_text(GTK_ENTRY(entry_manual_digits), digits);
    }

    int n = strlen(digits);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_result));

    if (n < 3) {
        gtk_text_buffer_set_text(buffer, "❌ กรุณาระบุตัวเลขที่ไม่ซ้ำกันอย่างน้อย 3 ตัวขึ้นไปนะคะ!", -1);
        gtk_label_set_text(GTK_LABEL(label_summary), "รอการระบุตัวเลข...");
        return;
    }

    GString *result_str = g_string_new("");
    int total_combinations = 0;
    char temp[16];

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            for (int k = 0; k < n; k++) {
                if (i != j && i != k && j != k) {
                    snprintf(temp, sizeof(temp), "%c%c%c   ", digits[i], digits[j], digits[k]);
                    g_string_append(result_str, temp);
                    total_combinations++;
                    if (total_combinations % 10 == 0) {
                        g_string_append(result_str, "\n");
                    }
                }
            }
        }
    }

    gtk_text_buffer_set_text(buffer, result_str->str, -1);
    g_string_free(result_str, TRUE);

    int cost = total_combinations * 1;
    int payout = 900;
    int profit = payout - cost;

    char summary_report[512];
    snprintf(summary_report, sizeof(summary_report),
             "✨ นำเลข [%s] จำนวน %d ตัวมาจัดเรียง\n"
             "📊 ได้เลข 3 ตัวบนทั้งหมด: %d ชุด\n"
             "💰 ลงทุนชุดละ 1 บาท รวมเป็นเงิน: %d บาท\n"
             "🏆 เงินรางวัลที่จะได้รับ (บาทละ 900): %d บาท\n"
             "📈 ผลกำไรสุทธิที่จะได้รับ: %d บาท!",
             digits, n, total_combinations, cost, payout, profit);

    gtk_label_set_text(GTK_LABEL(label_summary), summary_report);
}

void on_report_clicked(GtkWidget *widget, gpointer data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_result));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *combinations = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    const char *summary = gtk_label_get_text(GTK_LABEL(label_summary));

    if (strlen(combinations) == 0 || g_str_has_prefix(summary, "รอการประมวลผล")) {
        GtkWidget *error_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                                         GTK_MESSAGE_WARNING,
                                                         GTK_BUTTONS_OK,
                                                         "⚠️ ไม่พบข้อมูลผลลัพธ์! กรุณากดปุ่มวิเคราะห์เลขก่อนส่งออกรายงานนะคะ");
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        g_free(combinations);
        return;
    }

    FILE *file = fopen("geng_lottery_report.txt", "w");

    if (file != NULL) {
        fprintf(file, "==================================================\n");
        fprintf(file, "      รายงานผลการวิเคราะห์ระบบ Geng-Lottery\n");
        fprintf(file, "==================================================\n\n");
        fprintf(file, "%s\n\n", summary);
        fprintf(file, "--------------------------------------------------\n");
        fprintf(file, "รายชื่อชุดตัวเลขย้อนหลัง 20 งวดจริงที่ระบบใช้คำนวณ:\n");
        fprintf(file, "--------------------------------------------------\n");
        for(int i = 0; i < 20; i++) {
            fprintf(file, "งวดที่ %d: %s\n", i+1, historical_draws[i]);
        }
        fprintf(file, "\n--------------------------------------------------\n");
        fprintf(file, "รายชื่อชุดตัวเลขทั้งหมดที่จัดเรียงเรียบร้อยแล้ว:\n");
        fprintf(file, "--------------------------------------------------\n");
        fprintf(file, "%s\n", combinations);
        
        // ✨ [เพิ่มจุดที่ 2] กล่องข้อความสนับสนุนค่าน้ำชาท้ายไฟล์รายงาน Text
        fprintf(file, "==================================================\n");
        fprintf(file, "💖 หากท่านชอบโปรแกรมนี้และอยากช่วยอุดหนุน สามารถโอนบริจาคได้ที่:\n");
        fprintf(file, "   ธ.กสิกรณ์ไทย (K-bank)\n");
        fprintf(file, "   เลขที่บัญชี : 1192455177\n");
        fprintf(file, "   ชื่อบัญชี : นาย ธรรมสรณ์ มุสิกพันธ์\n");
        fprintf(file, "🙏 ขอบพระคุณทุกการสนับสนุนและทุกน้ำใจมากๆ ค่ะ\n");
        fprintf(file, "==================================================\n");
        
        fclose(file);

        GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                                           GTK_MESSAGE_INFO,
                                                           GTK_BUTTONS_OK,
                                                           "💾 ส่งออกรายงานเรียบร้อยแล้ว\n\nไฟล์บันทึกชื่อ geng_lottery_report.txt");
        gtk_dialog_run(GTK_DIALOG(success_dialog));
        gtk_widget_destroy(success_dialog);
    }
    g_free(combinations);
}

void on_mode_changed(GtkWidget *widget, gpointer data) {
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_auto))) {
        gtk_widget_set_sensitive(entry_manual_digits, FALSE);
        gtk_widget_set_sensitive(spin_auto_count, TRUE);
    } else {
        gtk_widget_set_sensitive(entry_manual_digits, TRUE);
        gtk_widget_set_sensitive(spin_auto_count, FALSE);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    int status_mode = 0;

    if (fetch_and_save_live_data()) {
        status_mode = 0;
    } else if (read_local_database()) {
        status_mode = 1;
    } else {
        status_mode = 2;
        const char* backup[20] = {"176","984","186","362","962","713","603","478","343","443","507","318","108","931","609","019","605","903","043","603"};
        for(int i=0; i<20; i++) strcpy(historical_draws[i], backup[i]);

        FILE *file = fopen(LOCAL_DB_PATH, "w");
        if (file) {
            fprintf(file, "[\n");
            for(int i = 0; i < 20; i++) {
                fprintf(file, "  \"%s\"%s\n", backup[i], (i == 19) ? "" : ",");
            }
            fprintf(file, "]\n");
            fclose(file);
        }
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Geng-Lottery Analyzer v1.3 (Hybrid DB)");
    // 📐 ขยายขนาดความสูงจาก 680 เป็น 760 เพื่อเปิดพื้นที่ให้กล่องรับบริจาคด้านล่างอย่างพอดีค่ะ
    gtk_window_set_default_size(GTK_WINDOW(window), 550, 760);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GError *error = NULL;
    gtk_window_set_icon_from_file(GTK_WINDOW(window), "/home/geng/Desktop/Geng Project/icon-lottery.png", &error);
    if (error != NULL) {
        g_error_free(error);
    }

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *lbl_net = gtk_label_new(NULL);
    if (status_mode == 0) {
        gtk_label_set_markup(GTK_LABEL(lbl_net), "<span foreground='#22c55e'>🟢 Online: อัปเดตสถิติล่าสุดจากเว็บ และบันทึกไฟล์ลงเครื่องสำเร็จ</span>");
    } else if (status_mode == 1) {
        gtk_label_set_markup(GTK_LABEL(lbl_net), "<span foreground='#f59e0b'>🟡 Offline Mode: อ่านข้อมูลสถิติจากไฟล์ในเครื่องคอมพิวเตอร์เรียบร้อย</span>");
    } else {
        gtk_label_set_markup(GTK_LABEL(lbl_net), "<span foreground='#ef4444'>🔴 System Backup: ไม่สามารถติดต่อเว็บ/ไฟล์ในเครื่องได้ (ใช้ฐานข้อมูลสำรอง)</span>");
    }
    gtk_box_pack_start(GTK_BOX(main_box), lbl_net, FALSE, FALSE, 0);

    GtkWidget *frame_mode = gtk_frame_new(" 🔮 เลือกวิธีการทำงาน ");
    gtk_box_pack_start(GTK_BOX(main_box), frame_mode, FALSE, FALSE, 0);

    GtkWidget *box_mode = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box_mode), 10);
    gtk_container_add(GTK_CONTAINER(frame_mode), box_mode);

    GtkWidget *box_manual = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    radio_manual = gtk_radio_button_new_with_label(NULL, "ป้อนตัวเลขที่ชอบด้วยตนเอง:");
    entry_manual_digits = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_manual_digits), "เช่น 024578");
    gtk_box_pack_start(GTK_BOX(box_manual), radio_manual, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_manual), entry_manual_digits, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_mode), box_manual, FALSE, FALSE, 0);

    GtkWidget *box_auto = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    radio_auto = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_manual), "ดึงเลขจากสถิติที่มาน้อยที่สุด 20 งวดล่าสุด:");

    GtkAdjustment *adj = gtk_adjustment_new(6.0, 3.0, 10.0, 1.0, 1.0, 0.0);
    spin_auto_count = gtk_spin_button_new(adj, 1.0, 0);

    gtk_box_pack_start(GTK_BOX(box_auto), radio_auto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_auto), spin_auto_count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_mode), box_auto, FALSE, FALSE, 0);

    gtk_widget_set_sensitive(spin_auto_count, FALSE);

    g_signal_connect(radio_manual, "toggled", G_CALLBACK(on_mode_changed), NULL);
    g_signal_connect(radio_auto, "toggled", G_CALLBACK(on_mode_changed), NULL);

    GtkWidget *box_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), box_actions, FALSE, FALSE, 5);

    GtkWidget *btn_calculate = gtk_button_new_with_label("🚀 เริ่มวิเคราะห์แปรผลเลข 3 ตัวบน");
    gtk_box_pack_start(GTK_BOX(box_actions), btn_calculate, TRUE, TRUE, 0);
    g_signal_connect(btn_calculate, "clicked", G_CALLBACK(on_calculate_clicked), NULL);

    GtkWidget *btn_report = gtk_button_new_with_label("📋 ออกรายงาน (Report Text)");
    gtk_box_pack_start(GTK_BOX(box_actions), btn_report, TRUE, TRUE, 0);
    g_signal_connect(btn_report, "clicked", G_CALLBACK(on_report_clicked), NULL);

    GtkWidget *frame_result = gtk_frame_new(" 📋 รายการชุดตัวเลขที่ระบบวิเคราะห์ได้ ");
    gtk_box_pack_start(GTK_BOX(main_box), frame_result, TRUE, TRUE, 0);

    GtkWidget *scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(frame_result), scroll_window);

    text_view_result = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_result), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view_result), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view_result), 10);
    gtk_container_add(GTK_CONTAINER(scroll_window), text_view_result);

    GtkWidget *frame_summary = gtk_frame_new(" 💡 แผงวิเคราะห์สัดส่วนการลงทุนและการทำกำไร ");
    gtk_box_pack_start(GTK_BOX(main_box), frame_summary, FALSE, FALSE, 5);

    label_summary = gtk_label_new("รอการประมวลผลคำนวณสูตรหวย...");
    gtk_label_set_justify(GTK_LABEL(label_summary), GTK_JUSTIFY_LEFT);

    gtk_widget_set_margin_start(label_summary, 12);
    gtk_widget_set_margin_end(label_summary, 12);
    gtk_widget_set_margin_top(label_summary, 12);
    gtk_widget_set_margin_bottom(label_summary, 12);

    gtk_container_add(GTK_CONTAINER(frame_summary), label_summary);

    // ✨ [เพิ่มจุดที่ 1] สร้างกรอบกล่องรับบริจาคโชว์บนตัวโปรแกรม GUI ด้านล่างสุด
    GtkWidget *frame_donate = gtk_frame_new(" 💖 สนับสนุนค่าน้ำชานักพัฒนา (Donation) ");
    gtk_box_pack_start(GTK_BOX(main_box), frame_donate, FALSE, FALSE, 5);

    GtkWidget *lbl_donate = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_donate),
                         "<b>หากท่านชอบโปรแกรมนี้และอยากช่วยอุดหนุนสามารถโอนบริจาคได้ที่</b>\n"
                         "🏦 <b>ธ.กสิกรณ์ไทย (K-bank)</b>\n"
                         "🆔 <b>เลขที่บัญชี :</b> 1192455177\n"
                         "👤 <b>ชื่อบัญชี :</b> นาย ธรรมสรณ์ มุสิกพันธ์");
    gtk_label_set_justify(GTK_LABEL(lbl_donate), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_start(lbl_donate, 10);
    gtk_widget_set_margin_end(lbl_donate, 10);
    gtk_widget_set_margin_top(lbl_donate, 10);
    gtk_widget_set_margin_bottom(lbl_donate, 10);
    gtk_container_add(GTK_CONTAINER(frame_donate), lbl_donate);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
