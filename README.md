Gigablast - an open source search engine
========================================

An open source web and enterprise search engine and spider/crawler.

This is a fork of the original Gigablast project available at https://github.com/gigablast/open-source-search-engine/. This version is heavily modified by Privacore, and tailored for our use. It is *not* a drop-in replacement for the original Gigablast.

Modifications by Privacore
--------------------------

Our aim is *not* to maintain backwards compatibility with the original Gigablast data files. 

| Feature  | Description |
| ------------- | ------------- |
| Multi-threading | Many improvements have been made with regards to multi-threading and general optimizations.|
| Stability | Numerous general bugfixes and major improvements in thread safety.|
| Data formats | Posdb is being changed to store a complete copy of a page in a single Posdb file, rather than spreading out a page content across multiple files and merging the data in memory + handling delete keys at query time. A new index file will point to the file containing the newest version of a document.|
| Data file merging | Our version will use a dedicated drive for merging, instead of merging + deleting part files on-the-fly on the same data drive. We will create a completely merged file on the merge drive, temporarily make GB use that file for queries, delete the original files, copy the newly merged file back to the 'production drive', switch back query handling to that drive and delete the temporary file.|
| Alerting | Start script improved to send alerts if GB crashes.|
| Trace log | Lots of options to add very detailed trace log to different parts of the code.|
| Summaries | Improvements in search results summary generation.|
| Language detection | Google's CLD2 library integrated to improve language detection.|
| Code removed | About half of the original source has been removed, e.g. Diffbot specific integrations.|
| Disk space | Lots of 'junk' removed from the Posdb data files, reducing space usage significantly. This means that if you use our version with old Gigablast data files, data will not be cleaned up correctly when re-indexing a page. You will need to rebuild the Posdb data files.|
|...|and much more...|

Migrating Gigablast to our fork
-------------------------------

| Step  | Description |
| ------------- | ------------- |
| Backup! | There, you have been warned.. |
| Build | git clone https://github.com/privacore/open-source-search-engine.git <br>git submodule init <br>git submodule update<br>make -j4<br>make dist|
| Copy | Stop your running GB instances. Copy the files contained in the new gb-[date]-[rev].tar.gz file to your GB instance 0.|
| Install | Go to your GB instance 0 and do a './gb install' to copy the binary and needed files to all instances.|
| Remove files | Remove the posdb files from your collections |
| Start | './gb start' from your instance 0 and you should be on your way.|
| Rebuild | Rebuild the posdb data files through the web UI. This is needed because we store less data in posdb than the original version, and GB cannot clean this 'junk' data up when re-indexing pages.|


RUNNING GIGABLAST
-----------------
See <a href=html/faq.html>html/faq.html</a> for all administrative documentation including the quick start instructions.

Alternatively, visit http://www.gigablast.com/faq.html

CODE ARCHITECTURE
-----------------
See <a href=html/developer.html>html/developer.html</a> for all code documentation.

Alternatively, visit http://www.gigablast.com/developer.html

SUPPORT
-------
Privacore does not provide paid support for Gigablast. We refer you to the original project at https://github.com/gigablast/open-source-search-engine/ and the owner Matt Wells. He has a Pro version you can buy which include support options.

We provide limited support for our fork, primarily for active contributors.

