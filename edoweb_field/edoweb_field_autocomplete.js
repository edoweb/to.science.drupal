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
  jQuery.fn.extend({
    propAttr: $.fn.prop || $.fn.attr
  });
  Drupal.behaviors.edoweb_field_autocomplete = {
    attach: function (context, settings) {
      window.location.hash = 'focus';
      $('.edoweb_autocomplete_widget').each(function(index, element) {
        var field_name = $(this).attr('class').split(/\s+/)[1];
        var bundle_name = $(this).attr('class').split(/\s+/)[2];
        $(this).autocomplete({
          source: Drupal.settings.basePath + 'edoweb/autocomplete/' + bundle_name + '/' + field_name,
          minLength: 2,
          select: function(event, ui) {
            var search_input = $('input[name="edoweb_autocomplete_widget[' + field_name + '][search]"]');
            var search_button = $('input[name="edoweb_autocomplete_widget[' + field_name + '][submit]"]');
            search_input.val(ui.item.label);
            search_button.trigger('click');
            return false;
          }
        });
      });
    }
  };

})(jQuery);
