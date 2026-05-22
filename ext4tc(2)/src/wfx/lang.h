#pragma once
#ifndef LANG_H
#define LANG_H

// -------------------------------------------------------
//  Локалізація ext4tc — двомовна підтримка EN / UK
//
//  Використання:
//    L10n::S("key")          — повертає const char* поточною мовою
//    L10n::SetLanguage("UK") — перемикає мову (викликається з LoadConfig)
//
//  Рядки зберігаються у двох статичних таблицях.
//  Ключі — прості ASCII-рядки (без пробілів).
//  Додавання нової мови: скопіювати блок UK, замінити значення.
// -------------------------------------------------------

#include <unordered_map>
#include <string>

namespace L10n {

// -------------------------------------------------------
//  Таблиця рядків
// -------------------------------------------------------
using StringTable = std::unordered_map<std::string, std::string>;

inline const StringTable& EN() {
    static const StringTable t = {
        // --- Admin warning ---
        { "warn_admin_title",    "ext4tc" },
        { "warn_admin_text",     "Warning: Plugin is not running as Administrator.\n"
                                 "Physical drive auto-detection is disabled." },

        // --- Mount errors ---
        { "err_mount_title",     "ext4tc \x97 Mount Error" },
        { "err_automount_pfx",   "Failed to auto-mount:\n" },
        { "err_no_src",          "Cannot open file or device:\n{PATH}\n\n"
                                 "Make sure the path is correct and you have "
                                 "sufficient access rights (run as Administrator "
                                 "for physical drives)." },
        { "err_sig_physical",    "No Ext2/3/4 filesystem found on:\n{PATH}\n\n"
                                 "The device does not contain a valid Ext superblock "
                                 "at the expected offset.\n"
                                 "Possible reasons:\n"
                                 "  \x95 Wrong drive index (try PhysicalDrive0, 1, 2\x85)\n"
                                 "  \x95 Partition offset is incorrect\n"
                                 "  \x95 The drive is formatted with a different filesystem" },
        { "err_sig_file",        "This file is not a valid Ext2/3/4 disk image:\n{PATH}\n\n"
                                 "Ext superblock signature (0xEF53) not found.\n"
                                 "Only raw disk images (.img, .bin, .raw) are supported.\n"
                                 "ISO, VMDK, VHDX and other container formats "
                                 "are not supported directly." },
        { "err_fs_corrupt",      "Ext2/3/4 signature found, but the filesystem appears "
                                 "to be corrupted or incomplete.\n\n"
                                 "Possible reasons:\n"
                                 "  \x95 Superblock is damaged\n"
                                 "  \x95 Wrong partition offset specified\n"
                                 "  \x95 The image was created incorrectly (partial dump)" },

        // --- Path validation (dialog) ---
        { "val_empty_title",     "Path is empty" },
        { "val_empty_text",      "Please enter a path to a disk image or physical drive.\n"
                                 "Example: C:\\disk.img  or  \\\\.\\PhysicalDrive1" },
        { "val_notfound_title",  "Invalid Path" },
        { "val_notfound_text",   "File not found or access denied:\n{PATH}\n\n"
                                 "Please check the path and try again." },
        { "val_isdir_title",     "Invalid Path" },
        { "val_isdir_text",      "The specified path is a folder, not a disk image file.\n"
                                 "Please select an .img, .bin or .raw file." },

        // --- Dialog labels ---
        { "dlg_mount_title",     "Mount Ext2/3/4 Volume" },
        { "dlg_config_title",    "Ext4TC Plugin Settings" },
        { "dlg_path_label",      "Path:" },
        { "dlg_browse_btn",      "Browse..." },
        { "dlg_readonly_chk",    "Read-only access" },
        { "dlg_readonly_cfg",    "Read-only access (default for new mounts)" },
        { "dlg_mount_btn",       "Mount" },
        { "dlg_ok_btn",          "OK" },
        { "dlg_cancel_btn",      "Cancel" },
        { "dlg_language_lbl",    "Language:" },
        { "dlg_browse_title",    "Select Ext2/3/4 disk image" },
        { "dlg_browse_filter",   "Disk images (*.img;*.bin;*.raw)\0*.img;*.bin;*.raw\0"
                                 "All files (*.*)\0*.*\0" },

        // --- Properties ---
        { "prop_title",          "Properties" },
        { "prop_format",         "Path: {SUB}\nInode: {INUM}\nSize: {SIZE} bytes\n" },

        // --- Mount new volume entry ---
        { "mount_new_entry",     "< Mount new volume >" },

        // --- Log / status ---
        { "log_scan_found",      "ext4tc: disk scan found {N} Ext partition(s)" },
        { "log_op_list",         "List directory" },
        { "log_op_get",          "Get file" },
        { "log_op_put",          "Put file" },
        { "log_op_rename",       "Rename/Move" },
        { "log_op_delete",       "Delete" },
        { "log_op_attr",         "Attributes" },
        { "log_op_exec",         "Execute" },
        { "log_op_calcsize",     "Calculate size" },
        { "log_op_search",       "Search" },
        { "log_op_searchtext",   "Search text" },
        { "log_op_sync",         "Synchronize" },
        { "log_op_unknown",      "Unknown" },
        { "log_status_fmt",      "ext4tc: {OP} \x97 {DIR} [{PHASE}]" },
        { "log_phase_start",     "start" },
        { "log_phase_end",       "end" },
    };
    return t;
}

inline const StringTable& UK() {
    static const StringTable t = {
        // --- Admin warning ---
        { "warn_admin_title",    "ext4tc" },
        { "warn_admin_text",     "Увага: плагін запущено без прав адміністратора.\n"
                                 "Автоматичне виявлення фізичних дисків вимкнено." },

        // --- Mount errors ---
        { "err_mount_title",     "ext4tc \x97 Помилка монтування" },
        { "err_automount_pfx",   "Не вдалось автоматично змонтувати:\n" },
        { "err_no_src",          "Неможливо відкрити файл або пристрій:\n{PATH}\n\n"
                                 "Перевірте правильність шляху та наявність "
                                 "достатніх прав доступу (для фізичних дисків "
                                 "потрібні права адміністратора)." },
        { "err_sig_physical",    "Файлову систему Ext2/3/4 не знайдено на:\n{PATH}\n\n"
                                 "Пристрій не містить коректного суперблоку Ext "
                                 "за очікуваним зміщенням.\n"
                                 "Можливі причини:\n"
                                 "  \x95 Невірний індекс диску (спробуйте PhysicalDrive0, 1, 2\x85)\n"
                                 "  \x95 Неправильне зміщення розділу\n"
                                 "  \x95 Диск відформатований іншою файловою системою" },
        { "err_sig_file",        "Цей файл не є коректним образом диску Ext2/3/4:\n{PATH}\n\n"
                                 "Сигнатуру суперблоку Ext (0xEF53) не знайдено.\n"
                                 "Підтримуються лише необроблені образи дисків (.img, .bin, .raw).\n"
                                 "Формати ISO, VMDK, VHDX та інші контейнери "
                                 "не підтримуються напряму." },
        { "err_fs_corrupt",      "Сигнатуру Ext2/3/4 знайдено, але файлова система "
                                 "виглядає пошкодженою або неповною.\n\n"
                                 "Можливі причини:\n"
                                 "  \x95 Суперблок пошкоджено\n"
                                 "  \x95 Вказано неправильне зміщення розділу\n"
                                 "  \x95 Образ створено некоректно (неповний дамп)" },

        // --- Path validation (dialog) ---
        { "val_empty_title",     "Порожній шлях" },
        { "val_empty_text",      "Введіть шлях до образу диску або фізичного пристрою.\n"
                                 "Приклад: C:\\disk.img  або  \\\\.\\PhysicalDrive1" },
        { "val_notfound_title",  "Невірний шлях" },
        { "val_notfound_text",   "Файл не знайдено або доступ заборонено:\n{PATH}\n\n"
                                 "Перевірте шлях та спробуйте знову." },
        { "val_isdir_title",     "Невірний шлях" },
        { "val_isdir_text",      "Вказаний шлях є папкою, а не образом диску.\n"
                                 "Будь ласка, оберіть файл .img, .bin або .raw." },

        // --- Dialog labels ---
        { "dlg_mount_title",     "Монтувати том Ext2/3/4" },
        { "dlg_config_title",    "Налаштування плагіна Ext4TC" },
        { "dlg_path_label",      "Шлях:" },
        { "dlg_browse_btn",      "Огляд..." },
        { "dlg_readonly_chk",    "Лише читання" },
        { "dlg_readonly_cfg",    "Лише читання (за замовчуванням для нових томів)" },
        { "dlg_mount_btn",       "Монтувати" },
        { "dlg_ok_btn",          "OK" },
        { "dlg_cancel_btn",      "Скасувати" },
        { "dlg_language_lbl",    "Мова:" },
        { "dlg_browse_title",    "Оберіть образ диску Ext2/3/4" },
        { "dlg_browse_filter",   "Образи диску (*.img;*.bin;*.raw)\0*.img;*.bin;*.raw\0"
                                 "Всі файли (*.*)\0*.*\0" },

        // --- Properties ---
        { "prop_title",          "Властивості" },
        { "prop_format",         "Шлях: {SUB}\nInode: {INUM}\nРозмір: {SIZE} байт\n" },

        // --- Mount new volume entry ---
        { "mount_new_entry",     "< Монтувати новий том >" },

        // --- Log / status ---
        { "log_scan_found",      "ext4tc: знайдено розділів Ext: {N}" },
        { "log_op_list",         "Перегляд каталогу" },
        { "log_op_get",          "Отримати файл" },
        { "log_op_put",          "Записати файл" },
        { "log_op_rename",       "Перейменування/Переміщення" },
        { "log_op_delete",       "Видалення" },
        { "log_op_attr",         "Атрибути" },
        { "log_op_exec",         "Виконання" },
        { "log_op_calcsize",     "Розрахунок розміру" },
        { "log_op_search",       "Пошук" },
        { "log_op_searchtext",   "Пошук тексту" },
        { "log_op_sync",         "Синхронізація" },
        { "log_op_unknown",      "Невідомо" },
        { "log_status_fmt",      "ext4tc: {OP} \x97 {DIR} [{PHASE}]" },
        { "log_phase_start",     "початок" },
        { "log_phase_end",       "кінець" },
    };
    return t;
}

// -------------------------------------------------------
//  Стан локалізації (поточна мова)
// -------------------------------------------------------
inline std::string& CurrentLang() {
    static std::string lang = "EN";
    return lang;
}

inline void SetLanguage(const std::string& lang) {
    CurrentLang() = (lang == "UK" || lang == "uk" || lang == "UA" || lang == "ua")
                    ? "UK" : "EN";
}

// -------------------------------------------------------
//  Основна функція отримання рядка
//  Повертає рядок поточною мовою; якщо ключ не знайдено —
//  повертає сам ключ (щоб помилки були видні відразу).
// -------------------------------------------------------
inline const char* S(const char* key) {
    const StringTable& tbl = (CurrentLang() == "UK") ? UK() : EN();
    auto it = tbl.find(key);
    if (it != tbl.end()) return it->second.c_str();
    // fallback: англійська
    auto it2 = EN().find(key);
    if (it2 != EN().end()) return it2->second.c_str();
    return key; // повертаємо ключ якщо зовсім не знайдено
}

// -------------------------------------------------------
//  Допоміжна: підстановка {PLACEHOLDER} у рядок
//  Використання: Fmt(S("err_no_src"), "{PATH}", path)
// -------------------------------------------------------
inline std::string Fmt(const char* tmpl,
                        const char* placeholder,
                        const std::string& value)
{
    std::string result = tmpl;
    size_t pos = result.find(placeholder);
    if (pos != std::string::npos)
        result.replace(pos, strlen(placeholder), value);
    return result;
}

} // namespace L10n

#endif // LANG_H
