#pragma once

#include "screens/components.h"

#include <memory>
#include <string>

namespace ciniva {

enum class LibraryKind { Movies, Shows };

std::unique_ptr<Page> make_home_page(Nav nav);
std::unique_ptr<Page> make_library_page(Nav nav, LibraryKind kind);
std::unique_ptr<Page> make_search_page(Nav nav, const std::string& query);
std::unique_ptr<Page> make_movie_page(Nav nav, int id);
std::unique_ptr<Page> make_show_page(Nav nav, int id);

}
