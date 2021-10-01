# About

regal-drupal is a collection of Drupal 7 modules that provide a front
end for [regal](https://github.com/edoweb/regal) (repository and
graph-based api for library data).

# Installation

regal-drupal depends on the librdf and curl modules for php5.
Installation on Ubuntu, your distribution may vary:

    $ sudo apt-get install php5-librdf
    $ sudo apt-get install php5-curl
    $ sudo apt-get install php5-intl

Clone the repository and submodules to Drupal's module directory:

    $ cd sites/all/modules
    $ git clone https://github.com/edoweb/regal-drupal.git
    $ git submodule update --init

Download non Drupal-core dependency modules:

    $ cd sites/all/modules
    $ curl https://ftp.drupal.org/files/projects/entity-7.x-1.1.tar.gz | tar xz
    $ curl https://ftp.drupal.org/files/projects/entity_js-7.x-1.0-alpha3.tar.gz | tar xz
    $ curl https://ftp.drupal.org/files/projects/ctools-7.x-1.3.tar.gz | tar xz

Activate "Edoweb Entities" module at (e.g. at
<http://localhost/drupal/?q=admin/modules>) and confirm activation of
dependency modules. Finally, set the host, user and password for the API
at <http://localhost/drupal/?q=admin/config/edoweb/storage>.

# Localization

To localize your Drupal installation, first activate the "Locale"
module. Then download your preferred language from
<https://localize.drupal.org/translate/languages/>. Navigate to
<http://localhost/drupal?q=admin/config/regional/translate/import>,
choose your language file and the language to import it into. To
localize regal-drupal (German file available [here](german.po), proceed
as describe above. Clear the cache to make sure all field instance
labels are updated.
