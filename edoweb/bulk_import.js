/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

(function($) {

  var import_entities = function(offset) {
    $.get(Drupal.settings.basePath + 'edoweb/config/resources?from=' + offset, function(data) {
      $.each(data, function(i, id) {
        entity_load_json('edoweb_basic', id).onload = function() {
          var message = $('<div />');
          if (this.status == 200) {
            var url = Drupal.settings.basePath + 'resource/' + id;
            message.append('<a href="' + url + '">Imported ' + id + '</a>');
            message.css('color', 'green');
          } else {
            message.text('Failed to import ' + id);
            message.css('color', 'red');
          }
          $("form#edoweb-bulk-import-form").after(message);
        };
        if (i == 9) import_entities(offset + 10);
      });
    });
  }

  Drupal.behaviors.bulk_import = {
    attach: function (context, settings) {
      $('form#edoweb-bulk-import-form').submit(function(event) {
        import_entities(0);
        return false;
      });
    }
  };

})(jQuery);

