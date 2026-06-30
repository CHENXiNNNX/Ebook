#include "ebook/ebook_app.hpp"
#include "test/storage/test_storage.hpp"


extern "C" void app_main(void)
{
    // ebook_app_run();
    test_storage();
}
