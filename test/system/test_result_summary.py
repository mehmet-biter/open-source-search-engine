import pytest
import mimetypes


def verify_summary(gb_api, httpserver, filename, expected_summary, custom_filename='', content_type='', delay=0.0):
    # guess content type
    if not content_type:
        content_type = mimetypes.guess_type(filename)[0]

    httpserver.serve_content(content=open(filename, 'rb').read(),
                             headers={'content-type': content_type})

    # format url
    file_url = httpserver.url + '/' + custom_filename

    # add url
    assert gb_api.add_url(file_url) == True

    if delay:
        from time import sleep
        sleep(delay)

    # verify result
    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1
    print(result['results'][0]['sum'])
    assert result['results'][0]['sum'] == expected_summary


def emoticon_expected_summary():
    return 'Li Europan lingues es membres del sam familie. Lor separat existentie es un myth. ' \
            'Por scientie, musica, sport etc, litot Europa usa li sam vocabular. Li lingues …'


@pytest.mark.parametrize('filename, expected_summary', [
    # filename                         expected_summary
    ('summary_emoticon_start.html',    emoticon_expected_summary()),
    ('summary_emoticon_middle.html',   emoticon_expected_summary()),
    ('summary_emoticon_end.html',      emoticon_expected_summary()),
])
def test_title_emoticon(gb_api, httpserver, filename, expected_summary):
    verify_summary(gb_api, httpserver, 'data/html/' + filename, expected_summary)
