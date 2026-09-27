#ifndef FILES_TESTS_H
#define FILES_TESTS_H

int run_files_tests();
void test_handles_closed_on_pterm(void);
void test_fforce_onto_gemdrive_file(void);
void test_pexec_from_another_current_drive(void);
void test_program_loaded_as_its_header_asks(void);
void test_gemdos_keeps_registers(void);

#endif  // FILES_TESTS_H
