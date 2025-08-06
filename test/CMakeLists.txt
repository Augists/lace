add_executable(test_barrier test_barrier.c)
target_link_libraries(test_barrier lace32)
add_test(test_barrier test_barrier)

add_executable(test_together test_together.c)
target_link_libraries(test_together lace)
add_test(test_together test_together)

add_executable(test_suspend test_suspend.c)
target_link_libraries(test_suspend lace)
add_test(test_suspend test_suspend)

add_executable(test_backoff test_backoff.c)
target_link_libraries(test_backoff lace)
add_test(test_backoff test_backoff)
