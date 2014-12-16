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

  Drupal.behaviors.edoweb_edit = {
    attach: function (context, settings) {
      var home_href = Drupal.settings.basePath + 'resource';
      if (document.location.pathname == home_href && '' != document.location.search) {
        localStorage.setItem('edoweb_search', document.location.search);
      }
      if (search = localStorage.getItem('edoweb_search')) {
        $('a[href="' + home_href + '"]').attr('href', home_href + search);
      }
      $('input#edit-delete', context).bind('click', function() {
        var confirmed = confirm('Möchten Sie den Eintrag unwideruflich löschen?');
        if (confirmed) {
          try {
            cut_entity = JSON.parse(localStorage.getItem('cut_entity'));
            if (Drupal.settings.edoweb.entity == cut_entity.remote_id) {
              localStorage.removeItem('cut_entity');
            }
          } catch(e) {
            localStorage.removeItem('cut_entity');
          }
          Drupal.edoweb.refreshTree();
        }
        return confirmed;
      });

      $('.tabs a', context).bind('click', function() {
        var href = $(this).attr('href');
        history.pushState({tree: true}, null, href);
        Drupal.edoweb.navigateTo(href);
        return false;
      });

      // Attach lookup overlay to page
      var modal_overlay = $('<div />').dialog({
        position: 'top',
        modal: true,
        autoOpen: false,
        width: '80%'
      });

      $('.edoweb.entity.edit', context).each(function() {
        var bundle = $(this).attr('data-entity-bundle');
        var entity = $(this);
        entity.css('margin-bottom', '2em');
        var additional_fields = $('<select><option>Feld hinzufügen</option></select>').change(function() {
          var instance = Drupal.settings.edoweb.fields[bundle][$(this).val()].instance;
          var field = createField(instance);
          activateFields(field, bundle, context);
          entity.find('.content').prepend(field);
          $(this).find('option:selected').remove();
          if ($(this).find('option').length == 1) {
            $(this).remove();
          }
        });
        $.each(Drupal.settings.edoweb.fields[bundle], function(index, value) {
          var instance = value['instance'];
          var field_class = getFieldClassName(instance);
          var existing_items = entity.find('.' + field_class);
          if (! existing_items.length && (instance['required'] || instance['settings']['default'])) {
            var field = createField(instance);
            entity.find('.content').prepend(field);
          } else if (! existing_items.length &&
                     ! instance['settings']['read_only'] &&
                     instance['settings']['metadata_type'] == 'descriptive')
          {
            var option = $('<option />').text(instance['label']).val(index);
            additional_fields.append(option);
          }
        });
        if (additional_fields.find('option').length > 1) {
          entity.before(additional_fields);
        }

        var submit_button = $('<button class="edoweb edit action" id="save-entity">Speichern</button>').bind('click', {entity: entity, bundle: bundle}, saveEntity);
        entity.after(submit_button);

        if (Drupal.settings.edoweb.primary_bundles.indexOf(entity.attr('data-entity-bundle')) != -1) {
          var template_select = $('<select><option>Satzschablone laden</option></select>').change(function() {
            var throbber = $('<div class="ajax-progress"><div class="throbber">&nbsp;</div></div>');
            $(this).after(throbber);
            entity_render_view('edoweb_basic', $(this).val()).onload = function() {
              template_select[0].selectedIndex = 0;
              throbber.remove();
              var entity_content = $(this.responseText).find('.content');
              var page_title = $(this.responseText).find('h2').text();
              Drupal.attachBehaviors(entity_content);
              activateFields(entity_content.find('.field'), bundle, context);
              entity.find('.content').replaceWith(entity_content);
              $('#page-title', context).text(page_title);
            };
          });
          $.get(Drupal.settings.basePath + 'edoweb/templates/' + bundle,
            function(data) {
              $.each(JSON.parse(data), function(i, entity) {
                $('<option />').text(entity['@id']).val(entity['@id']).appendTo(template_select);
              });
            }
          );
          additional_fields.after(template_select);
          var template_button = $('<button class="edoweb edit action" id="save-entity-template">Als Satzschablone Speichern</button>').bind('click', {entity: entity, bundle: bundle}, saveEntity);
          submit_button.after(template_button);
        }

        if (bundle == 'journal' || bundle == 'monograph') {
          var import_button = $('<button class="edoweb edit action">Importieren</button>').bind('click', function() {
            instance = {'bundle': bundle, 'field_name': ''}
            modal_overlay.html('<div />');
            refreshTable(modal_overlay, null, null, null, null, null, instance, function(uri) {
              entity_render_view('edoweb_basic', Drupal.edoweb.compact_uri(uri)).onload = function() {
                var entity_content = $(this.responseText).find('.content');
                var entity_parallel = entity_content.find('.field-name-field-edoweb-parallel');
                if (0 == entity_parallel.length) {
                  var instance = Drupal.settings.edoweb.fields[bundle]['field_edoweb_parallel'].instance;
                  entity_parallel = createField(instance);
                  entity_content.prepend(entity_parallel);
                }
                entity_content.find('.field-name-field-edoweb-identifier-ht').each(function() {
                  var hbzURI = 'lr:' + $(this).find('.field-item').text();
                  var field_item = $('<div class="field-item" rel="umbel:isLike">'
                    + '<a href="/resource/' + hbzURI + '" data-curie="' + hbzURI + '" resource="' + hbzURI + '" data-target-bundle="' + bundle + '">'
                    + Drupal.edoweb.expand_curie(hbzURI)
                    + '</a></div>');
                  entity_parallel.find('.field-items').append(field_item);
                  $(this).remove();
                });
                var page_title = $(this.responseText).find('h2').text();
                Drupal.attachBehaviors(entity_content);
                activateFields(entity_content.find('.field'), bundle, context);
                entity.find('.content').replaceWith(entity_content);
                $('#page-title', context).text(page_title);
              };
            });
            modal_overlay.dialog('open');
            return false;
          });
          submit_button.after(import_button);
        }

        activateFields(entity.find('.field'), bundle, context);

      });

      function saveEntity(e) {
        var entity = e.data.entity;
        var bundle = e.data.bundle;
        var button = $(this);
        var throbber = $('<div class="ajax-progress"><div class="throbber">&nbsp;</div></div>')
        $(this).after(throbber);
        $('button.edoweb.edit.action').hide();
        entity.find('[contenteditable]').each(function() {
          $(this).text($(this).text());
        });
        var rdf = entity.rdf();
        var topic = rdf.where('?s <http://xmlns.com/foaf/0.1/primaryTopic> ?o').get(0);
        var url = topic.s.value.toString();
        if ('save-entity-template' == button.attr('id')) {
          url += '?namespace=template';
        }
        var subject = topic.o;
        var post_data = rdf.databank.dump({
          format:'application/rdf+xml',
          serialize: true,
          namespaces: Drupal.settings.edoweb.namespaces
        });
        $.post(url, post_data, function(data, textStatus, jqXHR) {
          var resource_uri = jqXHR.getResponseHeader('X-Edoweb-Entity');
          button.trigger('insert', resource_uri);
          var href = Drupal.settings.basePath + 'resource/' + resource_uri;
          // Newly created resources are placed into the clipboard
          // and a real redirect is triggered.
          if (subject.type == 'bnode') {
            entity_load_json('edoweb_basic', resource_uri).onload = function() {
              if (bundle == 'monograph' || bundle == 'journal') {
                window.location = href;
              } else {
                localStorage.setItem('cut_entity', this.responseText);
                history.pushState({tree: true}, null, href);
                Drupal.edoweb.navigateTo(href);
              }
            };
          } else {
            history.pushState({tree: true}, null, href);
            Drupal.edoweb.navigateTo(href);
            Drupal.edoweb.refreshTree(context);
          }
          throbber.remove();
        });
        return false;
      }

      function getFieldName(field) {
        var cls = field.attr('class').split(' ');
        var field_name;
        $.each(cls, function(i, v) {
          if (v.indexOf('field-name-') == 0) {
            field_name = v.slice(11).replace(/-/g, '_');
          }
        });
        return field_name;
      }

      function getFieldClassName(instance) {
        var field_name = instance['field_name'];
        return 'field-name-' + field_name.replace(/_/g, '-');
      }

      function createField(instance) {
        var cls = 'field-name-' + instance['field_name'].replace(/_/g, '-');
        cls += ' field-type-' + instance['settings']['field_type'].replace(/_/g, '-');
        var field = $('<div class="field ' + cls + '"><div class="field-label">' + instance['label'] + ':&nbsp;</div><div class="field-items" /></div>');
        return field;
      }

      function createTextInput(instance, target) {
        var input = $('<div class="field-item" />')
          .attr('property', instance['settings']['predicates'].join(' '));
        enableTextInput(input);
        target.append(input);
      }

      function enableTextInput(field) {

        field.attr('contenteditable', true)
          .css('border', '1px solid grey')
          .css('margin-top', '0.3em')
          .css('padding', '0.1em')
          .css('min-height', '1.5em')
          .keydown(function(e) {
            var target = $(e.target);
            if (e.keyCode == 8 && ! target.text().length && target.siblings('.field-item').length) {
              placeCaretAtEnd(target.prev('.field-item').get(0));
              $(this).remove();
              return false;
            }
          });

        // Source: http://stackoverflow.com/a/4238971
        function placeCaretAtEnd(el) {
          el.focus();
          if (typeof window.getSelection != "undefined"
              && typeof document.createRange != "undefined") {
            var range = document.createRange();
            range.selectNodeContents(el);
            range.collapse(false);
            var sel = window.getSelection();
            sel.removeAllRanges();
            sel.addRange(range);
          } else if (typeof document.body.createTextRange != "undefined") {
            var textRange = document.body.createTextRange();
            textRange.moveToElementText(el);
            textRange.collapse(false);
            textRange.select();
          }
        }

      }


      function createLinkInput(instance, target) {
        modal_overlay.html('<div />');
        refreshTable(modal_overlay, null, null, null, null, null, instance, function(uri) {
          var input = $('<div class="field-item" />')
            .attr('rel', instance['settings']['predicates'].join(' '));
          var target_bundles = instance['settings']['handler_settings']['target_bundles'];
          for (target_bundle in target_bundles) break;
          var link = $('<a />').attr('href', uri).text(uri)
            .attr('data-target-bundle', target_bundle)
            .attr('data-curie', Drupal.edoweb.compact_uri(uri))
            .attr('resource', Drupal.edoweb.compact_uri(uri));
          input.append(link);
          target.append(input);
          Drupal.edoweb.entity_table(target);
        });
        modal_overlay.dialog('open');
      }

      function createUploadInput(instance, target) {
        var input = $('<input id="file" type="file" />');
        $('#save-entity', context).bind('insert', function(event, uri) {
          var files = $('#file').get(0).files;
          formData = new FormData();
          for (var i = 0; i < files.length; i++) {
            var file = files[i];
            formData.append('files[]', file, file.name);
          }
          var request = new XMLHttpRequest();
          request.open("POST", Drupal.settings.basePath + 'resource/' + uri + '/data', false);
          request.send(formData);
        });
        target.append(input);
      }

      function createOptionsInput(instance, target) {
        var input = $.get(
          Drupal.settings.basePath + 'edoweb_options_list/' + instance['field_name'],
          function(data) {
            var select = $(data);
            select.prepend('<option selected="selected">Sprache auswählen</option>');
            select.change(function() {
              var input = $('<div class="field-item" />')
                .attr('rel', instance['settings']['predicates'].join(' '))
                .attr('resource', $(this).find('option:selected').val())
                .text($(this).find('option:selected').text());
              target.append(input);
              $(this).remove();
            });
            target.append(select);
          }
        );
      }

      function activateFields(fields, bundle, context) {
        $.each(fields, function() {
          var field = $(this);
          var field_name = getFieldName(field);
          var instance = Drupal.settings.edoweb.fields[bundle][field_name]['instance'];
          switch (instance['widget']['type']) {
            case 'text_textarea':
            case 'text_textfield':
            case 'number':
              field.find('.field-items').each(function() {
                if ($(this).find('.field-item').length && ! instance['settings']['read_only']) {
                  enableTextInput($(this).find('.field-item'));
                } else if ($(this).find('.field-item').length < 1) {
                  createTextInput(instance, $(this));
                }
                if ((instance['settings']['cardinality'] == -1)
                    || ($(this).find('.field-item').length < instance['settings']['cardinality'])) {
                  var add_button = $('<a href="#"><span class="octicon octicon-plus" /></a>')
                    .bind('click', function() {
                      createTextInput(instance, field.find('.field-items'));
                      return false;
                    }).css('float', 'right').css('margin-right', '0.3em');
                  field.find('.field-label').append(add_button);
                }
              });
              break;
            case 'edoweb_autocomplete_widget':
              field.find('.field-items').each(function() {
                if (instance['settings']['metadata_type'] == 'descriptive'
                    && ((instance['settings']['cardinality'] == -1)
                    || ($(this).find('.field-item').length < instance['settings']['cardinality']))) {
                  var add_button = $('<a href="#"><span class="octicon octicon-link" /></a>')
                    .bind('click', function() {
                      createLinkInput(instance, field.find('.field-items'));
                      return false;
                    }).css('float', 'right').css('margin-right', '0.3em');
                  field.find('.field-label').append(add_button);
                }
              });
              break;
            case 'edoweb_upload_widget':
              field.find('.field-items').each(function() {
                if ($(this).find('.field-item').length < 1) {
                  createUploadInput(instance, $(this));
                }
              });
              break;
            case 'options_select':
              field.find('.field-items').each(function() {
                if ((instance['settings']['cardinality'] == -1)
                    || ($(this).find('.field-item').length < instance['settings']['cardinality'])) {
                  createOptionsInput(instance, $(this));
                }
              });
              break;
            default:
              console.log(instance['widget']['type']);
          }
        });
      }

    }
  };

  function refreshTable(container, page, sort, order, term, type, instance, callback) {
    if(!page) page = 0;
    if(!sort) sort = '';
    if(!order) order = '';
    if(!term) term = '';
    if(!type) type = '';

    var bundle_name = instance['bundle'];
    var field_name = instance['field_name'];

    var qurl = Drupal.settings.basePath + '?q=edoweb/search/' + bundle_name + '/' + field_name;
    var params = {
      page: page,
      sort: sort,
      order: order,
      'query[0][term]': term,
      'query[0][type]': type,
    };

    var throbber = $('<div class="ajax-progress"><div class="throbber">&nbsp;</div></div>')
    container.find('input[type="text"]').after(throbber);

    var request = jQuery.ajax({
      cache: false,
      url: qurl,
      data: params,
      dataType: 'text',
      error: function(request, status, error) {
        throbber.remove();
      },
      success: function(data, status, request) {
        throbber.remove();
        var html = $(data);

        html.find('a[data-bundle]').each(function() {
          if (!('person' == this.getAttribute('data-bundle'))
              && !('corporate_body' == this.getAttribute('data-bundle')))
          {
            $(this).remove();
          }

          $(this).bind('click', function(e) {
            var url = Drupal.settings.basePath + 'resource/add/' + this.getAttribute('data-bundle');
            $.get(url, function(data) {
              var form = $(data).find('.edoweb.entity.edit');
              container.html(form);
              Drupal.attachBehaviors(container);
              container.find('#save-entity').remove();
              var submit_button = $('<button id="save-entity">Speichern</button>').bind('click', function() {
                var button = $(this);
                var throbber = $('<div class="ajax-progress"><div class="throbber">&nbsp;</div></div>')
                $(this).replaceWith(throbber);
                container.find('[contenteditable]').each(function() {
                  $(this).text($(this).text());
                });
                var rdf = container.rdf();
                var topic = rdf.where('?s <http://xmlns.com/foaf/0.1/primaryTopic> ?o').get(0);
                var url = topic.s.value.toString();
                var post_data = rdf.databank.dump({format:'application/rdf+xml', serialize: true});
                $.post(url, post_data, function(data, textStatus, jqXHR) {
                  var resource_uri = jqXHR.getResponseHeader('X-Edoweb-Entity');
                  callback(Drupal.edoweb.expand_curie(resource_uri));
                  container.dialog('close');
                  throbber.remove();
                });
                return false;
              });
              container.append(submit_button);
            });
            return false;
          });
        });

        var type_selector = html.find('input[type=radio]');
        if (type_selector.length == 1) {
          type_selector.parent().hide();
        }
        type_selector.bind('change', function() {
          $(this).closest('form').find('input[name="op"]').click();
        });

        container.html(html);

        container.find('a[data-curie][data-target-bundle]').each(function() {
          Drupal.edoweb.entity_label($(this));
        });

        container.find('input[name="op"]').click(function() {
          var term = container.find('input[type="text"]').val();
          var target_type = $(this).closest('form').find('input[type=radio]:checked').first().val();
          refreshTable(container, null, null, null, term, target_type, instance, callback);
          return false;
        });

        container.find('th a').add('ul.pager a')
          .click(function(el, a, b, c) {
            var target_type = container.find('input[type=radio]:checked').first().val();
            var url = jQuery.url(el.currentTarget.getAttribute('href'));
            refreshTable(container, url.param('page'), url.param('sort'), url.param('order'), url.param('query[0][term]'), target_type, instance, callback);
            return false;
          });

        container.find('.sticky-enabled > tbody > tr').each(function() {
          var row = jQuery(this);
          jQuery(this).children('td').last()
            .append('<button>Übernehmen</button>')
            .bind('click', function(event) {
              container.dialog('close');
              var resource_uri = row.children('td').first().children('a').first().attr('href');
              callback(resource_uri);
              return false;
            });
        });

        Drupal.edoweb.hideEmptyTableColumns(container.find('.sticky-enabled'));
        Drupal.edoweb.hideTableHeaders(container.find('.sticky-enabled'));
        Drupal.attachBehaviors(container);

      }
    });
    Drupal.edoweb.pending_requests.push(request);
  }

})(jQuery);
