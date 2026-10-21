// content/movie_captions (remake/content/movie_captions.gd): the caption shown on a decoded frame
// and the set of an ending movie. The sets below are written by hand; the expected captions are
// read off them by hand (the last change at or before the frame).
#include "content/movie_captions.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(movie_captions, caption_at_and_set_of) {
    gd::Ref<MovieCaptions> c = gd::make<MovieCaptions>();
    c->pictures.append(nullptr);
    c->pictures.append(nullptr);
    c->places.append(gd::Vector2i(0, 0));
    c->places.append(gd::Vector2i(8, 200));
    // Set 1: caption 0 from frame 10, none (-1) from frame 40, caption 1 from frame 50; set 2 names
    // a caption that does not exist (5).
    c->sets.append(gd::PI32{10, 0, 40, -1, 50, 1});
    c->sets.append(gd::PI32{1, 5});
    c->movies = gd::PI32{0, 2, 1};
    t.expect_equal(c->caption_at(1, 9), gd::Int(-1), "before the first change: none");
    t.expect_equal(c->caption_at(1, 10), gd::Int(0), "frame 10: caption 0");
    t.expect_equal(c->caption_at(1, 39), gd::Int(0), "frame 39: still caption 0");
    t.expect_equal(c->caption_at(1, 40), gd::Int(-1), "frame 40: cleared");
    t.expect_equal(c->caption_at(1, 500), gd::Int(1), "after the last change: caption 1");
    t.expect_equal(c->caption_at(2, 3), gd::Int(-1), "a caption past the pictures: none");
    t.expect_equal(c->caption_at(0, 10), gd::Int(-1), "set 0: none");
    t.expect_equal(c->caption_at(3, 10), gd::Int(-1), "a set past the list: none");
    t.expect_equal(c->set_of(1), gd::Int(2), "ending movie 1: set 2");
    t.expect_equal(c->set_of(3), gd::Int(0), "a movie past the list: 0");
    t.expect_equal(c->set_of(-1), gd::Int(0), "a title movie (-1): 0");
}
